#define _GNU_SOURCE
#include "zcring.h"

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>   /* /proc/meminfo: there is no sysconf for huge page size */
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif
/* Defined here rather than pulled from <linux/memfd.h> for the same reason as
 * the futex constants below: the header must build against any libc, and
 * MFD_HUGETLB is missing from glibc's sys/mman.h on older distributions even
 * when the running kernel implements it. A kernel that does not gets EINVAL
 * from memfd_create and falls back — see §11. */
#ifndef MFD_HUGETLB
#define MFD_HUGETLB 0x0004U
#endif
#ifndef MADV_HUGEPAGE
#define MADV_HUGEPAGE 14
#endif

/* Raw futex ops. Spelled out rather than pulled from <linux/futex.h> so this
 * builds against any libc without dragging kernel headers into the public
 * header's include set.
 *
 * FUTEX_PRIVATE_FLAG is deliberately absent. The control block is a
 * MAP_SHARED memfd mapped at a different virtual address in every process, so
 * the kernel has to key the wait queue on the underlying page rather than on
 * the address. With the private flag this would work perfectly between
 * threads of one process and silently never wake anything across processes —
 * which is the only configuration that matters here. See §9 in zcring.h. */
#define ZC_FUTEX_WAIT 0
#define ZC_FUTEX_WAKE 1

int zc_futex_wait(_Atomic uint32_t *word, uint32_t expect,
                  const struct timespec *rel)
{
    long rc = syscall(SYS_futex, (uint32_t *)word, ZC_FUTEX_WAIT, expect,
                      rel, NULL, 0);
    return rc < 0 ? -1 : 0;
}

int zc_futex_wake(_Atomic uint32_t *word, int n)
{
    long rc = syscall(SYS_futex, (uint32_t *)word, ZC_FUTEX_WAKE, n,
                      NULL, NULL, 0);
    return (int)rc;
}

void zc_wake(zc_ring_t *r)
{
    zc_ctrl_t *c = r->ctrl;
    /* A read-modify-write that adds nothing, not a load — see zcring.h §9.
     * Adding zero still places this in ctrl->waiters' modification order,
     * which is what pairs it with the consumer's increment and forbids the
     * interleaving where the producer misses a waiter and that waiter misses
     * the message. Do not "optimise" this into an atomic_load: a load carries
     * no such guarantee, and the resulting lost wakeup is a hang, not a
     * slowdown. */
    if (atomic_fetch_add_explicit(&c->waiters, 0, memory_order_seq_cst) == 0)
        return;

    /* The consumer's only way to measure what being asleep cost it, and to
     * recover the true arrival time of a censored sample (§7). Free here: this
     * path is about to make a syscall regardless. */
    atomic_store_explicit(&c->wake_ts, zc_now_ns(), memory_order_relaxed);

    /* Bumping the generation closes the race against a consumer that has
     * already sampled futex_word but not yet entered FUTEX_WAIT: its wait
     * fails with EAGAIN instead of sleeping. */
    atomic_fetch_add_explicit(&c->futex_word, 1, memory_order_release);

    /* One wake for every sleeper. Notification is O(1) in the consumer count
     * exactly as publication is. */
    zc_futex_wake(&c->futex_word, INT_MAX);
}

static int zc_memfd(const char *name, unsigned flags)
{
#ifdef SYS_memfd_create
    return (int)syscall(SYS_memfd_create, name, flags);
#else
    (void)name; (void)flags;
    errno = ENOSYS;
    return -1;
#endif
}

static size_t zc_align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

/* ---- huge-page backing (§11) ---- */

/* Atomic because zc_create() may be called from several threads and this
 * caches across calls. The race is benign — every racer computes the same
 * value — but "benign" is not a thing ThreadSanitizer believes, and a TSan
 * report here would be indistinguishable from a real ordering bug in the
 * ring. Relaxed is enough: the value is a constant, not a flag guarding
 * other state. */
static _Atomic size_t zc_hpsz_cache = 0;

size_t zc_hugepage_size(void)
{
    size_t v = atomic_load_explicit(&zc_hpsz_cache, memory_order_relaxed);
    if (v) return v == (size_t)-1 ? 0 : v;

    /* There is no sysconf for this. /proc/meminfo's Hugepagesize is the
     * default pool's page size, which is the one MFD_HUGETLB uses when no
     * MFD_HUGE_* size flag is given — so it is exactly the right number. */
    v = 0;
    FILE *f = fopen("/proc/meminfo", "re");
    if (f) {
        char line[256];
        while (fgets(line, sizeof line, f)) {
            unsigned long kb;
            if (sscanf(line, "Hugepagesize: %lu kB", &kb) == 1) {
                v = (size_t)kb * 1024;
                break;
            }
        }
        fclose(f);
    }
    /* Must be a power of two for the alignment arithmetic below. */
    if (v & (v - 1)) v = 0;
    atomic_store_explicit(&zc_hpsz_cache, v ? v : (size_t)-1,
                          memory_order_relaxed);
    return v;
}

static _Atomic int zc_huge_policy = ZC_HUGE_AUTO;

void zc_set_hugepage_policy(int policy)
{
    atomic_store_explicit(&zc_huge_policy, policy, memory_order_relaxed);
}

int zc_backing(const zc_ring_t *r)
{
    if (!r || !r->ctrl) return ZC_BACKING_4K;
    uint32_t m = r->ctrl->mode;
    if (m & ZC_MODE_F_HUGETLB)   return ZC_BACKING_HUGETLB;
    if (m & ZC_MODE_F_HUGEALIGN) return ZC_BACKING_THP;
    return ZC_BACKING_4K;
}

const char *zc_backing_name(int backing)
{
    switch (backing) {
        case ZC_BACKING_HUGETLB: return "hugetlb";
        case ZC_BACKING_THP:     return "thp-advise";
        default:                 return "4k";
    }
}

/* mmap the ring at a huge-page-aligned virtual address.
 *
 * mmap(NULL) returns something page-aligned and nothing more, and THP will
 * not use a PMD for a region that does not start on a 2 MiB boundary — so
 * without this the madvise path is dead on arrival.
 *
 * Reserve length+align with PROT_NONE, then drop the real mapping into the
 * aligned middle with MAP_FIXED. MAP_FIXED over our own reservation replaces
 * it atomically, which is why the slack is unmapped afterwards rather than
 * before: unmapping first would open a window in which another thread's mmap
 * could take the address, and MAP_FIXED would then silently destroy it. */
static void *zc_mmap_aligned(int fd, size_t map_size, size_t align)
{
    size_t reserve = map_size + align;
    uint8_t *p = mmap(NULL, reserve, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) return MAP_FAILED;

    uint8_t *aligned = (uint8_t *)zc_align_up((size_t)p, align);
    void *base = mmap(aligned, map_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED | MAP_FIXED, fd, 0);
    if (base == MAP_FAILED) { munmap(p, reserve); return MAP_FAILED; }

    size_t head = (size_t)(aligned - p);
    if (head) munmap(p, head);
    if (reserve > head + map_size)
        munmap(aligned + map_size, reserve - head - map_size);
    return base;
}

static int zc_map(zc_ring_t *r, int fd, size_t map_size, uint32_t mode)
{
    /* Reproduce whatever the creator did. A hugetlbfs fd needs nothing: the
     * kernel returns a huge-page-aligned address and every page of it is
     * huge, whoever maps it. The THP path is the one where an attaching
     * process has to opt in for itself. */
    size_t align = (mode & ZC_MODE_F_HUGEALIGN) ? zc_hugepage_size() : 0;
    void *base = align ? zc_mmap_aligned(fd, map_size, align)
                       : mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                              MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) return -1;

    zc_ctrl_t *c = (zc_ctrl_t *)base;

    /* Arena only, never the control block: §11. Failure is ignored on
     * purpose — the mapping is already valid and the advice is an
     * optimisation, so a kernel that refuses it costs TLB entries, not
     * correctness. */
    if (align && c->arena_off < map_size)
        (void)madvise((uint8_t *)base + c->arena_off,
                      map_size - (size_t)c->arena_off, MADV_HUGEPAGE);

    r->base      = base;
    r->map_size  = map_size;
    r->ctrl      = c;
    r->slots     = (zc_slot_t *)((uint8_t *)base + c->slots_off);
    r->arena     = (uint8_t *)base + c->arena_off;
    r->mask      = c->slot_count - 1;
    r->slot_size = c->slot_size;
    /* Cached locally so zc_commit()'s per-message test is an L1 hit on a
     * private word rather than a load from the shared control block. */
    r->notify    = (c->mode & ZC_MODE_F_NOTIFY) ? 1u : 0u;
    r->fd        = fd;
    return 0;
}

/* One backing attempt: fd, size, and a mapping to initialise through. The
 * three steps are here together because the hugetlbfs failure that actually
 * happens in practice is the *third* one — memfd_create and ftruncate both
 * succeed against an empty pool, and only mmap reports ENOMEM, because
 * hugetlbfs takes its reservation at mmap time. A fallback that gave up after
 * the open would therefore never fire on the one system state it exists for. */
static int zc_open_backing(size_t map_size, int huge, int *fd_out, void **base_out)
{
    int fd = zc_memfd("zcring", MFD_CLOEXEC | (huge ? MFD_HUGETLB : 0u));
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)map_size) != 0) { close(fd); return -1; }
    void *base = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { close(fd); return -1; }
    *fd_out = fd;
    *base_out = base;
    return 0;
}

static int zc_create_mode(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size,
                          uint32_t mode)
{
    if (!r || slot_count < 2 || (slot_count & (slot_count - 1))) {
        errno = EINVAL;
        return -1;
    }
    memset(r, 0, sizeof(*r));

    long pg = sysconf(_SC_PAGESIZE);
    if (pg <= 0) pg = 4096;

    size_t arena_sz = (size_t)slot_count * slot_size;
    size_t hpsz     = zc_hugepage_size();
    int    policy   = atomic_load_explicit(&zc_huge_policy, memory_order_relaxed);

    if (policy == ZC_HUGE_REQUIRE && hpsz <= (size_t)pg) {
        errno = ENOTSUP;   /* asked for huge pages on a kernel that has none */
        return -1;
    }
    /* Huge pages only where they can exist and where the arena is big enough
     * to pay for the rounding (§11). REQUIRE bypasses the size test: it marks
     * a benchmark arm, and quietly serving it 4 KiB pages is precisely the
     * contamination it exists to rule out. */
    int want_huge = policy != ZC_HUGE_OFF && hpsz > (size_t)pg &&
                    (policy == ZC_HUGE_REQUIRE ||
                     arena_sz >= (size_t)ZC_HUGE_MIN_PAGES * hpsz);

    /* The alignment the arena is laid out to. At ZC_HUGE_OFF, and on any
     * system without huge pages, this is the page size and the layout is
     * byte-for-byte the pre-§11 one. */
    size_t align     = want_huge ? hpsz : (size_t)pg;
    size_t slots_off = zc_align_up(sizeof(zc_ctrl_t), ZC_CACHELINE);
    size_t slots_sz  = (size_t)slot_count * sizeof(zc_slot_t);
    size_t arena_off = zc_align_up(slots_off + slots_sz, align);
    size_t map_size  = zc_align_up(arena_off + arena_sz, align);

    int   fd   = -1;
    void *base = NULL;
    if (want_huge && zc_open_backing(map_size, 1, &fd, &base) == 0) {
        mode |= ZC_MODE_F_HUGETLB;
    } else {
        /* errno here is whatever the hugetlbfs attempt failed with — ENOMEM
         * for an empty pool, EINVAL/ENOSYS for a kernel without it. Returning
         * before it can be overwritten is what makes REQUIRE diagnosable. */
        if (want_huge && policy == ZC_HUGE_REQUIRE) return -1;
        if (zc_open_backing(map_size, 0, &fd, &base) != 0) return -1;
        /* Second choice: keep the huge-aligned layout and ask for THP over
         * the arena. Recorded only if the kernel accepts the advice, so a
         * build without CONFIG_TRANSPARENT_HUGEPAGE reports 4k rather than
         * claiming a THP it never got. Acceptance still is not proof the
         * pages are huge — see zc_backing(). */
        if (want_huge &&
            madvise((uint8_t *)base + arena_off, map_size - arena_off,
                    MADV_HUGEPAGE) == 0)
            mode |= ZC_MODE_F_HUGEALIGN;
    }

    zc_ctrl_t *c = (zc_ctrl_t *)base;
    memset(c, 0, sizeof(*c));
    c->magic      = ZC_MAGIC;
    c->version    = ZC_ABI_VERSION;
    c->mode       = mode;
    c->slot_count = slot_count;
    c->slot_size  = slot_size;
    c->slots_off  = slots_off;
    c->arena_off  = arena_off;
    c->map_size   = map_size;
    atomic_store_explicit(&c->head, 0, memory_order_relaxed);
    atomic_store_explicit(&c->tail, 0, memory_order_relaxed);
    atomic_store_explicit(&c->futex_word, 0, memory_order_relaxed);
    atomic_store_explicit(&c->waiters, 0, memory_order_relaxed);
    atomic_store_explicit(&c->wake_ts, 0, memory_order_relaxed);
    atomic_store_explicit(&c->gate_cache, 0, memory_order_relaxed);

    /* memset already zeroed the registry, which is ZC_CONS_FREE. Written
     * explicitly because the gate's correctness depends on it. */
    for (uint32_t i = 0; i < ZC_MAX_CONSUMERS; i++) {
        atomic_store_explicit(&c->cons[i].state, ZC_CONS_FREE,
                              memory_order_relaxed);
        atomic_store_explicit(&c->cons[i].cursor, 0, memory_order_relaxed);
        atomic_store_explicit(&c->cons[i].pid, 0, memory_order_relaxed);
        atomic_store_explicit(&c->cons[i].joins, 0, memory_order_relaxed);
    }

    /* Vyukov initialisation: slot i starts at seq i, meaning "empty and
     * awaiting the producer at position i". */
    zc_slot_t *slots = (zc_slot_t *)((uint8_t *)base + slots_off);
    for (uint32_t i = 0; i < slot_count; i++) {
        atomic_store_explicit(&slots[i].seq, (uint64_t)i, memory_order_relaxed);
        slots[i].len = 0;
    }
    atomic_thread_fence(memory_order_release);

    munmap(base, map_size);
    if (zc_map(r, fd, map_size, mode) != 0) { close(fd); return -1; }
    return 0;
}

int zc_create(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size)
{
    return zc_create_mode(r, slot_count, slot_size, ZC_MODE_UNICAST);
}

int zc_create_bcast(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size)
{
    return zc_create_mode(r, slot_count, slot_size, ZC_MODE_BROADCAST);
}

int zc_create_notify(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size)
{
    return zc_create_mode(r, slot_count, slot_size,
                          ZC_MODE_UNICAST | ZC_MODE_F_NOTIFY);
}

int zc_create_bcast_notify(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size)
{
    return zc_create_mode(r, slot_count, slot_size,
                          ZC_MODE_BROADCAST | ZC_MODE_F_NOTIFY);
}

int zc_attach(zc_ring_t *r, int fd)
{
    if (!r || fd < 0) { errno = EINVAL; return -1; }
    memset(r, 0, sizeof(*r));

    /* Peek the control block to learn the real size, then map it all.
     *
     * pread, not a throwaway mmap of the first page. On a hugetlbfs-backed
     * memfd (§11) the kernel rounds a mapping's length up to the huge page
     * size, so mapping sizeof(zc_ctrl_t) would map 2 MiB and the matching
     * munmap of 1344 bytes would fail with EINVAL — leaking a huge page of a
     * scarce global pool on every attach. Reading bytes has no such rule and
     * costs one fewer syscall besides. */
    zc_ctrl_t tmp;
    ssize_t got = pread(fd, &tmp, sizeof tmp, 0);
    if (got < 0) return -1;
    if ((size_t)got != sizeof tmp) { errno = EINVAL; return -1; }

    if (tmp.magic != ZC_MAGIC) { errno = EINVAL; return -1; }
    /* Layer 2 moved fields around in zc_ctrl_t. Attaching to a ring built by
     * a different ABI would misread every offset, so refuse rather than
     * produce garbage. */
    if (tmp.version != ZC_ABI_VERSION) { errno = EPROTO; return -1; }
    return zc_map(r, fd, (size_t)tmp.map_size, tmp.mode);
}

void zc_close(zc_ring_t *r)
{
    if (!r || !r->base) return;
    munmap(r->base, r->map_size);
    if (r->fd >= 0) close(r->fd);
    memset(r, 0, sizeof(*r));
}

int zc_fd(const zc_ring_t *r) { return r ? r->fd : -1; }

/* ---- broadcast registration ---- */

int zc_bcast_join(zc_ring_t *r)
{
    if (!r || !r->ctrl) { errno = EINVAL; return -1; }
    zc_ctrl_t *c = r->ctrl;
    /* Masked, not compared whole: the high bits of mode carry independent
     * flags (ZC_MODE_F_NOTIFY). A peer built before those flags existed
     * compares the whole word and refuses, which is the safe direction. */
    if ((c->mode & ZC_MODE_MASK) != ZC_MODE_BROADCAST) { errno = EINVAL; return -1; }

    for (uint32_t i = 0; i < ZC_MAX_CONSUMERS; i++) {
        uint32_t expect = ZC_CONS_FREE;
        /* FREE -> CLAIMED. While CLAIMED the gate ignores us, which is the
         * window we need to install a cursor before we start constraining
         * the producer. */
        if (!atomic_compare_exchange_strong_explicit(
                &c->cons[i].state, &expect, ZC_CONS_CLAIMED,
                memory_order_acq_rel, memory_order_relaxed))
            continue;

        atomic_store_explicit(&c->cons[i].pid, (uint32_t)getpid(),
                              memory_order_relaxed);
        atomic_fetch_add_explicit(&c->cons[i].joins, 1, memory_order_relaxed);
        atomic_store_explicit(&c->cons[i].cursor,
                              atomic_load_explicit(&c->head, memory_order_acquire),
                              memory_order_relaxed);

        /* CLAIMED -> ACTIVE. From here the producer gates on our cursor. */
        atomic_store_explicit(&c->cons[i].state, ZC_CONS_ACTIVE,
                              memory_order_release);

        /* The producer was free to advance head while we were CLAIMED, so the
         * cursor we installed may already be behind. Re-snap to the current
         * head: those messages were published before we existed and are not
         * ours. Safe to do after going ACTIVE because the producer is now
         * gated at the earlier cursor, so it cannot have lapped us, and this
         * store only ever moves the gate forward. */
        atomic_store_explicit(&c->cons[i].cursor,
                              atomic_load_explicit(&c->head, memory_order_acquire),
                              memory_order_release);
        return (int)i;
    }
    errno = ENOSPC;
    return -1;
}

void zc_bcast_leave(zc_ring_t *r, int id)
{
    if (!r || !r->ctrl || id < 0 || id >= ZC_MAX_CONSUMERS) return;
    /* Releasing the entry is sufficient to un-gate the producer: the gate
     * scan skips anything not ACTIVE. No slot is held, only a cursor. */
    atomic_store_explicit(&r->ctrl->cons[id].state, ZC_CONS_FREE,
                          memory_order_release);
}

int zc_bcast_reap(zc_ring_t *r)
{
    if (!r || !r->ctrl) { errno = EINVAL; return -1; }
    zc_ctrl_t *c = r->ctrl;
    int reaped = 0;

    for (uint32_t i = 0; i < ZC_MAX_CONSUMERS; i++) {
        if (atomic_load_explicit(&c->cons[i].state, memory_order_acquire)
            != ZC_CONS_ACTIVE)
            continue;
        uint32_t pid = atomic_load_explicit(&c->cons[i].pid,
                                            memory_order_relaxed);
        if (pid == 0) continue;

        /* ESRCH is the only signal that the owner is genuinely gone. EPERM
         * means it exists but belongs to another user — still alive, so still
         * entitled to hold the producer back. A merely slow consumer answers
         * here exactly as a healthy one does, which is the point: this evicts
         * the dead, never the late. */
        if (kill((pid_t)pid, 0) == 0 || errno != ESRCH) continue;

        uint32_t expect = ZC_CONS_ACTIVE;
        if (atomic_compare_exchange_strong_explicit(
                &c->cons[i].state, &expect, ZC_CONS_FREE,
                memory_order_acq_rel, memory_order_relaxed))
            reaped++;
    }
    return reaped;
}

/* Pass the ring's memfd to an unrelated process over a UNIX socket.
 * fork() inherits the fd for free; this is for the general case. */
int zc_send_fd(int sock, int fd)
{
    char buf[CMSG_SPACE(sizeof(int))];
    char dummy = 'z';
    struct iovec iov = { .iov_base = &dummy, .iov_len = 1 };
    struct msghdr msg = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = buf, .msg_controllen = sizeof(buf)
    };
    memset(buf, 0, sizeof(buf));
    struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type  = SCM_RIGHTS;
    cm->cmsg_len   = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &fd, sizeof(int));
    return sendmsg(sock, &msg, 0) < 0 ? -1 : 0;
}

int zc_recv_fd(int sock)
{
    char buf[CMSG_SPACE(sizeof(int))];
    char dummy = 0;
    struct iovec iov = { .iov_base = &dummy, .iov_len = 1 };
    struct msghdr msg = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = buf, .msg_controllen = sizeof(buf)
    };
    memset(buf, 0, sizeof(buf));
    if (recvmsg(sock, &msg, 0) <= 0) return -1;
    struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
    if (!cm || cm->cmsg_type != SCM_RIGHTS) { errno = EINVAL; return -1; }
    int fd;
    memcpy(&fd, CMSG_DATA(cm), sizeof(int));
    return fd;
}

#define _GNU_SOURCE
#include "zcring.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

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

static int zc_map(zc_ring_t *r, int fd, size_t map_size)
{
    void *base = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) return -1;

    zc_ctrl_t *c = (zc_ctrl_t *)base;
    r->base      = base;
    r->map_size  = map_size;
    r->ctrl      = c;
    r->slots     = (zc_slot_t *)((uint8_t *)base + c->slots_off);
    r->arena     = (uint8_t *)base + c->arena_off;
    r->mask      = c->slot_count - 1;
    r->slot_size = c->slot_size;
    r->fd        = fd;
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

    size_t slots_off = zc_align_up(sizeof(zc_ctrl_t), ZC_CACHELINE);
    size_t slots_sz  = (size_t)slot_count * sizeof(zc_slot_t);
    size_t arena_off = zc_align_up(slots_off + slots_sz, (size_t)pg);
    size_t arena_sz  = (size_t)slot_count * slot_size;
    size_t map_size  = zc_align_up(arena_off + arena_sz, (size_t)pg);

    int fd = zc_memfd("zcring", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)map_size) != 0) { close(fd); return -1; }

    void *base = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { close(fd); return -1; }

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
    if (zc_map(r, fd, map_size) != 0) { close(fd); return -1; }
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

int zc_attach(zc_ring_t *r, int fd)
{
    if (!r || fd < 0) { errno = EINVAL; return -1; }
    memset(r, 0, sizeof(*r));

    /* Peek the control block to learn the real size, then map it all. */
    void *hdr = mmap(NULL, sizeof(zc_ctrl_t), PROT_READ, MAP_SHARED, fd, 0);
    if (hdr == MAP_FAILED) return -1;
    zc_ctrl_t tmp = *(zc_ctrl_t *)hdr;
    munmap(hdr, sizeof(zc_ctrl_t));

    if (tmp.magic != ZC_MAGIC) { errno = EINVAL; return -1; }
    /* Layer 2 moved fields around in zc_ctrl_t. Attaching to a ring built by
     * a different ABI would misread every offset, so refuse rather than
     * produce garbage. */
    if (tmp.version != ZC_ABI_VERSION) { errno = EPROTO; return -1; }
    return zc_map(r, fd, (size_t)tmp.map_size);
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
    if (c->mode != ZC_MODE_BROADCAST) { errno = EINVAL; return -1; }

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

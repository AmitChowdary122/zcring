#define _GNU_SOURCE
#include "zcring.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
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

int zc_create(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size)
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
    c->slot_count = slot_count;
    c->slot_size  = slot_size;
    c->slots_off  = slots_off;
    c->arena_off  = arena_off;
    c->map_size   = map_size;
    atomic_store_explicit(&c->head, 0, memory_order_relaxed);
    atomic_store_explicit(&c->tail, 0, memory_order_relaxed);
    atomic_store_explicit(&c->futex_word, 0, memory_order_relaxed);
    atomic_store_explicit(&c->waiters, 0, memory_order_relaxed);

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

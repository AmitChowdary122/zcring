/*
 * zcring — zero-copy shared-memory ring transport
 * Layer 1: lock-free MPMC ring over a memfd-backed mapping.
 *
 * The defining property: reserve()/acquire() hand back raw pointers INTO the
 * shared mapping. The caller constructs and reads messages in place. There is
 * no memcpy anywhere in the data path — not in this header, not in zcring.c.
 * That is what makes this zero-copy rather than one-copy with a nicer name.
 */
#ifndef ZCRING_H
#define ZCRING_H

#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZC_CACHELINE 64
#define ZC_MAGIC     0x5A43524E47303031ULL /* "ZCRNG001" */

/* Per-slot descriptor. seq is the Vyukov sequence counter that carries both
 * ownership and the acquire/release edge for the payload bytes. */
typedef struct {
    _Atomic uint64_t seq;
    uint32_t         len;
    uint32_t         _pad;
} zc_slot_t;

/* Shared control block. head and tail sit on their own cache lines: if they
 * shared one, every producer commit would invalidate the consumer's line and
 * the false sharing would dominate the measurement. */
typedef struct {
    uint64_t magic;
    uint32_t slot_count;
    uint32_t slot_size;
    uint64_t slots_off;
    uint64_t arena_off;
    uint64_t map_size;

    _Alignas(ZC_CACHELINE) _Atomic uint64_t head;  /* producer cursor */
    _Alignas(ZC_CACHELINE) _Atomic uint64_t tail;  /* consumer cursor */

    /* Reserved for Layer 2 adaptive spin-then-futex notification. Present now
     * so the shared-memory ABI does not change when Layer 2 lands. */
    _Alignas(ZC_CACHELINE) _Atomic uint32_t futex_word;
    _Atomic uint32_t waiters;
} zc_ctrl_t;

/* Per-process handle. Never shared; each process attaches its own. */
typedef struct {
    zc_ctrl_t *ctrl;
    zc_slot_t *slots;
    uint8_t   *arena;
    uint32_t   mask;
    uint32_t   slot_size;
    void      *base;
    size_t     map_size;
    int        fd;
} zc_ring_t;

/* slot_count must be a power of two. Returns 0 on success, -1 on failure. */
int  zc_create(zc_ring_t *r, uint32_t slot_count, uint32_t slot_size);
int  zc_attach(zc_ring_t *r, int fd);
void zc_close(zc_ring_t *r);
int  zc_fd(const zc_ring_t *r);
int  zc_send_fd(int sock, int fd);
int  zc_recv_fd(int sock);

/* ---- fast path: must inline, so it lives in the header ---- */

/* Claim a slot. Returns a pointer into shared memory for in-place
 * construction, or NULL if the ring is full. */
static inline void *zc_reserve(zc_ring_t *r, uint64_t *pos_out)
{
    zc_ctrl_t *c = r->ctrl;
    uint64_t pos = atomic_load_explicit(&c->head, memory_order_relaxed);
    for (;;) {
        zc_slot_t *s = &r->slots[pos & r->mask];
        uint64_t seq = atomic_load_explicit(&s->seq, memory_order_acquire);
        int64_t diff = (int64_t)(seq - pos);
        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &c->head, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (diff < 0) {
            return NULL; /* full */
        } else {
            pos = atomic_load_explicit(&c->head, memory_order_relaxed);
        }
    }
    *pos_out = pos;
    return r->arena + (size_t)(pos & r->mask) * r->slot_size;
}

/* Publish. The release store is what makes the bytes the caller just wrote
 * visible to the consumer that observes this seq with acquire. */
static inline void zc_commit(zc_ring_t *r, uint64_t pos, uint32_t len)
{
    zc_slot_t *s = &r->slots[pos & r->mask];
    s->len = len;
    atomic_store_explicit(&s->seq, pos + 1, memory_order_release);
}

/* Claim a filled slot. Returns a pointer into shared memory for in-place
 * reading, or NULL if the ring is empty. */
static inline void *zc_acquire(zc_ring_t *r, uint64_t *pos_out, uint32_t *len_out)
{
    zc_ctrl_t *c = r->ctrl;
    uint64_t pos = atomic_load_explicit(&c->tail, memory_order_relaxed);
    zc_slot_t *s;
    for (;;) {
        s = &r->slots[pos & r->mask];
        uint64_t seq = atomic_load_explicit(&s->seq, memory_order_acquire);
        int64_t diff = (int64_t)(seq - (pos + 1));
        if (diff == 0) {
            if (atomic_compare_exchange_weak_explicit(
                    &c->tail, &pos, pos + 1,
                    memory_order_relaxed, memory_order_relaxed))
                break;
        } else if (diff < 0) {
            return NULL; /* empty */
        } else {
            pos = atomic_load_explicit(&c->tail, memory_order_relaxed);
        }
    }
    *pos_out = pos;
    *len_out = s->len;
    return r->arena + (size_t)(pos & r->mask) * r->slot_size;
}

/* Hand the slot back to the producer, one lap ahead. */
static inline void zc_release(zc_ring_t *r, uint64_t pos)
{
    zc_slot_t *s = &r->slots[pos & r->mask];
    atomic_store_explicit(&s->seq, pos + r->mask + 1, memory_order_release);
}

#ifdef __cplusplus
}
#endif
#endif /* ZCRING_H */

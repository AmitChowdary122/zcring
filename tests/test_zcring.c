/*
 * Correctness tests for the Layer 1 ring.
 *
 * A lock-free ring that is merely fast is worthless — the failure mode is a
 * silently dropped or duplicated message under contention, which a latency
 * benchmark will never reveal. These tests exist to make that failure loud.
 */
#define _GNU_SOURCE
#include "../src/zcring.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } \
} while (0)

#define SLOT_SIZE 256

/* Payload: [0..7] = id, remainder filled with (id ^ index) so corruption or
 * slot aliasing shows up as a content mismatch rather than passing silently. */
static void fill(void *p, uint64_t id)
{
    uint8_t *b = p;
    memcpy(b, &id, sizeof(id));
    for (uint32_t i = 8; i < SLOT_SIZE; i++) b[i] = (uint8_t)(id ^ i);
}

static int verify(const void *p, uint64_t *id_out)
{
    const uint8_t *b = p;
    uint64_t id;
    memcpy(&id, b, sizeof(id));
    for (uint32_t i = 8; i < SLOT_SIZE; i++)
        if (b[i] != (uint8_t)(id ^ i)) return -1;
    *id_out = id;
    return 0;
}

static void test_basic(void)
{
    printf("basic reserve/commit/acquire/release\n");
    zc_ring_t r;
    CHECK(zc_create(&r, 8, SLOT_SIZE) == 0, "zc_create failed");

    uint64_t pos, id = 0; uint32_t len;
    CHECK(zc_acquire(&r, &pos, &len) == NULL, "empty ring returned a message");

    void *p = zc_reserve(&r, &pos);
    CHECK(p != NULL, "reserve on empty ring returned NULL");
    fill(p, 42);
    zc_commit(&r, pos, SLOT_SIZE);

    p = zc_acquire(&r, &pos, &len);
    CHECK(p != NULL, "acquire after commit returned NULL");
    CHECK(len == SLOT_SIZE, "len mismatch: got %u", len);
    CHECK(verify(p, &id) == 0, "payload corrupted");
    CHECK(id == 42, "id mismatch: got %llu", (unsigned long long)id);
    zc_release(&r, pos);

    CHECK(zc_acquire(&r, &pos, &len) == NULL, "ring should be empty again");
    zc_close(&r);
}

static void test_full(void)
{
    printf("full/empty boundaries\n");
    zc_ring_t r;
    CHECK(zc_create(&r, 4, SLOT_SIZE) == 0, "zc_create failed");

    uint64_t pos; uint32_t len;
    for (int i = 0; i < 4; i++) {
        void *p = zc_reserve(&r, &pos);
        CHECK(p != NULL, "reserve %d failed on non-full ring", i);
        fill(p, (uint64_t)i);
        zc_commit(&r, pos, SLOT_SIZE);
    }
    CHECK(zc_reserve(&r, &pos) == NULL, "reserve succeeded on full ring");

    void *p = zc_acquire(&r, &pos, &len);
    CHECK(p != NULL, "acquire on full ring failed");
    zc_release(&r, pos);
    CHECK(zc_reserve(&r, &pos) != NULL, "reserve failed after a slot was freed");

    zc_close(&r);
}

/* ---- concurrent exactly-once delivery ---- */

#define N_PROD   4
#define N_CONS   4
#define PER_PROD 50000
#define TOTAL    (N_PROD * PER_PROD)

static zc_ring_t g_ring;
static _Atomic uint32_t *g_seen;   /* per-id delivery count */
static _Atomic uint64_t  g_consumed;
static _Atomic uint32_t  g_corrupt;

static void *producer(void *arg)
{
    uint64_t base = (uint64_t)(uintptr_t)arg * PER_PROD;
    for (uint64_t i = 0; i < PER_PROD; i++) {
        uint64_t pos; void *p;
        while (!(p = zc_reserve(&g_ring, &pos))) sched_yield();
        fill(p, base + i);
        zc_commit(&g_ring, pos, SLOT_SIZE);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    for (;;) {
        if (atomic_load(&g_consumed) >= TOTAL) return NULL;
        uint64_t pos, id; uint32_t len;
        void *p = zc_acquire(&g_ring, &pos, &len);
        if (!p) { sched_yield(); continue; }
        if (verify(p, &id) != 0 || id >= TOTAL) atomic_fetch_add(&g_corrupt, 1);
        else atomic_fetch_add(&g_seen[id], 1);
        zc_release(&g_ring, pos);
        atomic_fetch_add(&g_consumed, 1);
    }
}

static void test_concurrent(void)
{
    printf("concurrent %d producers / %d consumers, %d messages\n",
           N_PROD, N_CONS, TOTAL);
    CHECK(zc_create(&g_ring, 1024, SLOT_SIZE) == 0, "zc_create failed");
    g_seen = calloc(TOTAL, sizeof(*g_seen));
    atomic_store(&g_consumed, 0);
    atomic_store(&g_corrupt, 0);

    pthread_t pt[N_PROD], ct[N_CONS];
    for (long i = 0; i < N_CONS; i++) pthread_create(&ct[i], NULL, consumer, (void *)i);
    for (long i = 0; i < N_PROD; i++) pthread_create(&pt[i], NULL, producer, (void *)i);
    for (int i = 0; i < N_PROD; i++) pthread_join(pt[i], NULL);
    for (int i = 0; i < N_CONS; i++) pthread_join(ct[i], NULL);

    uint32_t lost = 0, dup = 0;
    for (uint64_t i = 0; i < TOTAL; i++) {
        uint32_t n = atomic_load(&g_seen[i]);
        if (n == 0) lost++;
        else if (n > 1) dup++;
    }
    CHECK(atomic_load(&g_corrupt) == 0, "%u corrupted payloads",
          atomic_load(&g_corrupt));
    CHECK(lost == 0, "%u messages lost", lost);
    CHECK(dup == 0, "%u messages delivered more than once", dup);

    free(g_seen);
    zc_close(&g_ring);
}

/* ---- cross-process over an inherited memfd ---- */

static void test_cross_process(void)
{
    printf("cross-process via inherited memfd\n");
    const uint32_t n = 10000;
    zc_ring_t r;
    CHECK(zc_create(&r, 256, SLOT_SIZE) == 0, "zc_create failed");

    _Atomic uint32_t *bad = mmap(NULL, sizeof(*bad), PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_store(bad, 0);

    pid_t pid = fork();
    if (pid == 0) {
        for (uint32_t i = 0; i < n; i++) {
            uint64_t pos, id; uint32_t len; void *p;
            while (!(p = zc_acquire(&r, &pos, &len))) sched_yield();
            if (verify(p, &id) != 0 || id != i) atomic_fetch_add(bad, 1);
            zc_release(&r, pos);
        }
        _exit(0);
    }
    for (uint32_t i = 0; i < n; i++) {
        uint64_t pos; void *p;
        while (!(p = zc_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_commit(&r, pos, SLOT_SIZE);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(atomic_load(bad) == 0, "%u mismatched messages across processes",
          atomic_load(bad));

    munmap(bad, sizeof(*bad));
    zc_close(&r);
}

/* ---- Layer 2: broadcast fan-out ----
 *
 * The invariant under test is stronger than Layer 1's. Unicast asserts
 * exactly-once across the consumer set; broadcast asserts exactly-once *per
 * consumer* — every consumer sees every message, in order, none missing and
 * none duplicated. That is only a checkable claim because the slow-consumer
 * policy is backpressure rather than drop (see §2 in zcring.h). */

#define BC_CONS  3
#define BC_MSGS  20000

static zc_ring_t bc_ring;
static _Atomic uint32_t bc_bad[BC_CONS];
static _Atomic uint32_t bc_got[BC_CONS];
static _Atomic uint32_t bc_ready;

static void *bc_consumer(void *arg)
{
    long k = (long)arg;
    int id = zc_bcast_join(&bc_ring);
    if (id < 0) { atomic_fetch_add(&bc_bad[k], 1); return NULL; }
    atomic_fetch_add(&bc_ready, 1);

    for (uint64_t want = 0; want < BC_MSGS; want++) {
        uint64_t pos, got; uint32_t len; void *p;
        while (!(p = zc_bcast_acquire(&bc_ring, id, &pos, &len))) sched_yield();
        /* Ordering is implicit: cursor advances by exactly one per release,
         * so a gap or repeat shows up as the wrong id here. */
        if (verify(p, &got) != 0 || got != want) atomic_fetch_add(&bc_bad[k], 1);
        else atomic_fetch_add(&bc_got[k], 1);
        zc_bcast_release(&bc_ring, id, pos);
    }
    zc_bcast_leave(&bc_ring, id);
    return NULL;
}

static void test_bcast_fanout(void)
{
    printf("broadcast fan-out, 1 producer / %d consumers, %d messages\n",
           BC_CONS, BC_MSGS);
    /* Deliberately small: 16 slots against 20000 messages guarantees the
     * producer is gated constantly, so the backpressure path is what is
     * actually under test rather than an incidental fast case. */
    CHECK(zc_create_bcast(&bc_ring, 16, SLOT_SIZE) == 0, "zc_create_bcast failed");
    atomic_store(&bc_ready, 0);
    for (int i = 0; i < BC_CONS; i++) {
        atomic_store(&bc_bad[i], 0);
        atomic_store(&bc_got[i], 0);
    }

    pthread_t ct[BC_CONS];
    for (long i = 0; i < BC_CONS; i++) pthread_create(&ct[i], NULL, bc_consumer, (void *)i);
    /* Consumers join at the current head, so publishing before they have all
     * registered would legitimately lose them messages. */
    while (atomic_load(&bc_ready) < BC_CONS) sched_yield();

    for (uint64_t i = 0; i < BC_MSGS; i++) {
        uint64_t pos; void *p;
        while (!(p = zc_bcast_reserve(&bc_ring, &pos))) sched_yield();
        fill(p, i);
        zc_bcast_commit(&bc_ring, pos, SLOT_SIZE);
    }
    for (int i = 0; i < BC_CONS; i++) pthread_join(ct[i], NULL);

    for (int i = 0; i < BC_CONS; i++) {
        CHECK(atomic_load(&bc_bad[i]) == 0, "consumer %d: %u wrong/corrupt messages",
              i, atomic_load(&bc_bad[i]));
        CHECK(atomic_load(&bc_got[i]) == BC_MSGS,
              "consumer %d: saw %u of %d messages", i,
              atomic_load(&bc_got[i]), BC_MSGS);
    }
    zc_close(&bc_ring);
}

/* The gate must never let the producer overwrite a slot a consumer has not
 * passed. Verified directly: a consumer that stops releasing must stall the
 * producer within one lap, and never further. */
static void test_bcast_backpressure(void)
{
    printf("broadcast backpressure gates the producer at one lap\n");
    zc_ring_t r;
    CHECK(zc_create_bcast(&r, 8, SLOT_SIZE) == 0, "zc_create_bcast failed");

    int id = zc_bcast_join(&r);
    CHECK(id >= 0, "join failed");

    uint64_t pos; void *p;
    uint32_t wrote = 0;
    while ((p = zc_bcast_reserve(&r, &pos)) != NULL) {
        fill(p, wrote);
        zc_bcast_commit(&r, pos, SLOT_SIZE);
        if (++wrote > 64) break;   /* guard: must have stalled well before here */
    }
    CHECK(wrote == 8, "producer wrote %u before stalling, expected 8 (ring size)",
          wrote);
    CHECK(zc_bcast_lag(&r) == 8, "lag %llu, expected 8",
          (unsigned long long)zc_bcast_lag(&r));

    /* One consume must free exactly one slot, not more. */
    uint64_t cpos, id_got; uint32_t len;
    void *cp = zc_bcast_acquire(&r, id, &cpos, &len);
    CHECK(cp != NULL, "acquire failed on a full ring");
    CHECK(verify(cp, &id_got) == 0 && id_got == 0, "wrong message at cursor 0");
    zc_bcast_release(&r, id, cpos);

    CHECK(zc_bcast_reserve(&r, &pos) != NULL, "reserve failed after one release");
    CHECK(zc_bcast_reserve(&r, &pos) == NULL, "reserve gave back two slots for one release");

    zc_bcast_leave(&r, id);
    /* Departure must un-gate immediately, or a leaving consumer would be
     * indistinguishable from a stalled one. */
    CHECK(zc_bcast_reserve(&r, &pos) != NULL, "leave did not un-gate the producer");
    zc_close(&r);
}

/* A consumer that dies without leaving freezes its cursor and would gate the
 * producer forever. This is the bounded-recovery path. */
static void test_bcast_reap(void)
{
    printf("broadcast reclaims cursors of consumers that died without leaving\n");
    zc_ring_t r;
    CHECK(zc_create_bcast(&r, 8, SLOT_SIZE) == 0, "zc_create_bcast failed");

    _Atomic uint32_t *joined = mmap(NULL, sizeof(*joined), PROT_READ | PROT_WRITE,
                                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_store(joined, 0);

    pid_t pid = fork();
    if (pid == 0) {
        int id = zc_bcast_join(&r);
        atomic_store(joined, id >= 0 ? 1u : 2u);
        _exit(0);   /* dies holding an ACTIVE entry, cursor frozen at 0 */
    }
    while (atomic_load(joined) == 0) sched_yield();
    CHECK(atomic_load(joined) == 1, "child failed to join");

    /* kill(pid,0) succeeds on a zombie, so the child must be waited for
     * before its cursor is reclaimable. Documented in §3 of zcring.h. */
    int st = 0;
    waitpid(pid, &st, 0);

    uint64_t pos; void *p;
    uint32_t wrote = 0;
    while ((p = zc_bcast_reserve(&r, &pos)) != NULL) {
        zc_bcast_commit(&r, pos, SLOT_SIZE);
        if (++wrote > 64) break;
    }
    CHECK(wrote == 8, "dead consumer did not gate the producer (wrote %u)", wrote);

    CHECK(zc_bcast_reap(&r) == 1, "reap did not reclaim the dead consumer");
    CHECK(zc_bcast_active(&r) == 0, "entry still active after reap");
    CHECK(zc_bcast_reserve(&r, &pos) != NULL, "producer still gated after reap");

    munmap(joined, sizeof(*joined));
    zc_close(&r);
}

/* A live-but-slow consumer must NOT be reclaimed — that distinction is the
 * whole justification for backpressure over dropping. */
static void test_bcast_reap_spares_slow(void)
{
    printf("broadcast reap evicts the dead, not the merely slow\n");
    zc_ring_t r;
    CHECK(zc_create_bcast(&r, 8, SLOT_SIZE) == 0, "zc_create_bcast failed");

    int id = zc_bcast_join(&r);   /* this process: alive, never consuming */
    CHECK(id >= 0, "join failed");

    uint64_t pos;
    while (zc_bcast_reserve(&r, &pos)) zc_bcast_commit(&r, pos, SLOT_SIZE);

    CHECK(zc_bcast_reap(&r) == 0, "reap evicted a live consumer");
    CHECK(zc_bcast_active(&r) == 1, "live consumer lost its registration");
    CHECK(zc_bcast_reserve(&r, &pos) == NULL, "producer un-gated by a live consumer");

    zc_bcast_leave(&r, id);
    zc_close(&r);
}

/* Fan-out across real process boundaries, not just threads in one address
 * space — the sharing has to be genuine for the zero-copy claim to mean
 * anything. */
static void test_bcast_cross_process(void)
{
    printf("broadcast fan-out across %d forked processes\n", BC_CONS);
    const uint32_t n = 5000;
    zc_ring_t r;
    CHECK(zc_create_bcast(&r, 64, SLOT_SIZE) == 0, "zc_create_bcast failed");

    _Atomic uint32_t *bad   = mmap(NULL, sizeof(*bad) * 2, PROT_READ | PROT_WRITE,
                                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    _Atomic uint32_t *ready = bad + 1;
    atomic_store(bad, 0);
    atomic_store(ready, 0);

    pid_t kids[BC_CONS];
    for (int k = 0; k < BC_CONS; k++) {
        kids[k] = fork();
        if (kids[k] == 0) {
            int id = zc_bcast_join(&r);
            if (id < 0) { atomic_fetch_add(bad, 1); _exit(1); }
            atomic_fetch_add(ready, 1);
            for (uint64_t want = 0; want < n; want++) {
                uint64_t pos, got; uint32_t len; void *p;
                while (!(p = zc_bcast_acquire(&r, id, &pos, &len))) sched_yield();
                if (verify(p, &got) != 0 || got != want) atomic_fetch_add(bad, 1);
                zc_bcast_release(&r, id, pos);
            }
            zc_bcast_leave(&r, id);
            _exit(0);
        }
    }
    while (atomic_load(ready) < BC_CONS) sched_yield();

    for (uint64_t i = 0; i < n; i++) {
        uint64_t pos; void *p;
        while (!(p = zc_bcast_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_bcast_commit(&r, pos, SLOT_SIZE);
    }
    for (int k = 0; k < BC_CONS; k++) { int st = 0; waitpid(kids[k], &st, 0); }

    CHECK(atomic_load(bad) == 0, "%u wrong messages across processes",
          atomic_load(bad));
    munmap(bad, sizeof(*bad) * 2);
    zc_close(&r);
}

int main(void)
{
    /* Line-buffer stdout. When it is a pipe the default is block buffering,
     * and every fork() below would duplicate whatever is still sitting in the
     * buffer — producing a test log with each line repeated once per child,
     * which is exactly the sort of noise a real failure hides in. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    test_basic();
    test_full();
    test_concurrent();
    test_cross_process();
    test_bcast_fanout();
    test_bcast_backpressure();
    test_bcast_reap();
    test_bcast_reap_spares_slow();
    test_bcast_cross_process();

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nall checks passed\n");
    return 0;
}

/*
 * Correctness tests for the Layer 1 ring.
 *
 * A lock-free ring that is merely fast is worthless — the failure mode is a
 * silently dropped or duplicated message under contention, which a latency
 * benchmark will never reveal. These tests exist to make that failure loud.
 */
#define _GNU_SOURCE
#include "../src/zcring.h"

#include <errno.h>
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

/* ---- Layer 2: adaptive spin-then-futex notification ----
 *
 * Two things have to be true and neither is visible in a latency number.
 * First, delivery must still be exact when consumers sleep — a lost wakeup
 * looks exactly like a hang, and a hang in CI looks exactly like a slow
 * machine. Second, consumers must actually *sleep*: a policy that quietly
 * degenerates to spinning would pass every delivery check while delivering
 * none of the point. The gap here (200us against a wake cost of a few us) is
 * chosen so the learned budget must collapse well below it and blocking must
 * dominate, so n_blocks is a real assertion and not a coincidence.
 */

#define NT_MSGS    600
#define NT_GAP_NS  200000ULL   /* 200us: far above any plausible wake cost */

static void test_notify_unicast(void)
{
    printf("adaptive notification: unicast delivery with a sleeping consumer\n");
    zc_ring_t r;
    CHECK(zc_create_notify(&r, 64, SLOT_SIZE) == 0, "zc_create_notify failed");

    /* [0] mismatches, [1] messages seen, [2] waits, [3] blocks */
    _Atomic uint64_t *st = mmap(NULL, sizeof(*st) * 4, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    for (int i = 0; i < 4; i++) atomic_store(&st[i], 0);

    pid_t pid = fork();
    if (pid == 0) {
        zc_waiter_t w;
        zc_waiter_init(&w, ZC_BUDGET_SHIFT);
        for (uint64_t want = 0; want < NT_MSGS; want++) {
            uint64_t pos, got; uint32_t len;
            /* Unbounded wait: if notification is broken this hangs, which is
             * the honest failure mode. A timeout here would convert a lost
             * wakeup into a passing test with a slow run. */
            void *p = zc_wait(&r, &w, &pos, &len, 0);
            if (!p || verify(p, &got) != 0 || got != want)
                atomic_fetch_add(&st[0], 1);
            else
                atomic_fetch_add(&st[1], 1);
            zc_release(&r, pos);
        }
        atomic_store(&st[2], w.n_waits);
        atomic_store(&st[3], w.n_blocks);
        _exit(0);
    }

    uint64_t next = zc_now_ns();
    for (uint64_t i = 0; i < NT_MSGS; i++) {
        next += NT_GAP_NS;
        while (zc_now_ns() < next) { /* pace, so the consumer really idles */ }
        uint64_t pos; void *p;
        while (!(p = zc_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_commit(&r, pos, SLOT_SIZE);   /* wakes only if someone is asleep */
    }
    int stt = 0;
    waitpid(pid, &stt, 0);

    CHECK(atomic_load(&st[0]) == 0, "%llu wrong/missing messages under notification",
          (unsigned long long)atomic_load(&st[0]));
    CHECK(atomic_load(&st[1]) == NT_MSGS, "consumer saw %llu of %d messages",
          (unsigned long long)atomic_load(&st[1]), NT_MSGS);
    /* The policy must have learned to sleep. At a 200us gap the CPU budget
     * drives the spin threshold to a few percent of that, so the overwhelming
     * majority of waits must end in a block. */
    CHECK(atomic_load(&st[3]) > (uint64_t)(NT_MSGS / 2),
          "consumer blocked only %llu times in %llu waits — the budget did not "
          "converge and it is effectively still spinning",
          (unsigned long long)atomic_load(&st[3]),
          (unsigned long long)atomic_load(&st[2]));

    munmap(st, sizeof(*st) * 4);
    zc_close(&r);
}

/* Same guarantees for fan-out: one wake must serve every sleeper, and the
 * per-consumer exactly-once invariant must survive blocking. */
static void test_notify_bcast(void)
{
    printf("adaptive notification: broadcast wakes all %d sleeping consumers\n",
           BC_CONS);
    zc_ring_t r;
    CHECK(zc_create_bcast_notify(&r, 64, SLOT_SIZE) == 0,
          "zc_create_bcast_notify failed");

    _Atomic uint32_t *sh = mmap(NULL, sizeof(*sh) * 3, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    _Atomic uint32_t *bad = sh, *ready = sh + 1, *blocked = sh + 2;
    atomic_store(bad, 0); atomic_store(ready, 0); atomic_store(blocked, 0);

    pid_t kids[BC_CONS];
    for (int k = 0; k < BC_CONS; k++) {
        kids[k] = fork();
        if (kids[k] == 0) {
            int id = zc_bcast_join(&r);
            if (id < 0) { atomic_fetch_add(bad, 1); _exit(1); }
            zc_waiter_t w;
            zc_waiter_init(&w, ZC_BUDGET_SHIFT);
            atomic_fetch_add(ready, 1);
            for (uint64_t want = 0; want < NT_MSGS; want++) {
                uint64_t pos, got; uint32_t len;
                void *p = zc_bcast_wait(&r, &w, id, &pos, &len, 0);
                if (!p || verify(p, &got) != 0 || got != want)
                    atomic_fetch_add(bad, 1);
                zc_bcast_release(&r, id, pos);
            }
            if (w.n_blocks > NT_MSGS / 2) atomic_fetch_add(blocked, 1);
            zc_bcast_leave(&r, id);
            _exit(0);
        }
    }
    while (atomic_load(ready) < BC_CONS) sched_yield();

    uint64_t next = zc_now_ns();
    for (uint64_t i = 0; i < NT_MSGS; i++) {
        next += NT_GAP_NS;
        while (zc_now_ns() < next) { }
        uint64_t pos; void *p;
        while (!(p = zc_bcast_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_bcast_commit(&r, pos, SLOT_SIZE);
    }
    for (int k = 0; k < BC_CONS; k++) { int stt = 0; waitpid(kids[k], &stt, 0); }

    CHECK(atomic_load(bad) == 0, "%u wrong/missing messages under notification",
          atomic_load(bad));
    /* Every consumer, not just one: a single FUTEX_WAKE with an unbounded
     * count is what makes notification O(1) in N, and a bug that woke only
     * the first sleeper would still deliver correctly, just slowly. */
    CHECK(atomic_load(blocked) == BC_CONS,
          "only %u of %d consumers actually slept", atomic_load(blocked), BC_CONS);

    munmap(sh, sizeof(*sh) * 3);
    zc_close(&r);
}

/* The ski-rental floor (§7) has to hold at the other end of the rate range:
 * when messages arrive faster than a wake costs, the policy must stay in the
 * spin regime and keep the path syscall-free. This is the property a fixed
 * constant cannot have, so it is worth asserting rather than assuming. */
static void test_notify_fast_arrivals_do_not_block(void)
{
    printf("adaptive notification: fast arrivals stay in the spin regime\n");
    zc_ring_t r;
    CHECK(zc_create_notify(&r, 1024, SLOT_SIZE) == 0, "zc_create_notify failed");

    _Atomic uint64_t *st = mmap(NULL, sizeof(*st) * 2, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_store(&st[0], 0); atomic_store(&st[1], 0);

    const uint64_t n = 20000;
    pid_t pid = fork();
    if (pid == 0) {
        zc_waiter_t w;
        zc_waiter_init(&w, ZC_BUDGET_SHIFT);
        for (uint64_t want = 0; want < n; want++) {
            uint64_t pos, got; uint32_t len;
            void *p = zc_wait(&r, &w, &pos, &len, 0);
            if (!p || verify(p, &got) != 0 || got != want)
                atomic_fetch_add(&st[0], 1);
            zc_release(&r, pos);
        }
        atomic_store(&st[1], w.n_blocks);
        _exit(0);
    }
    for (uint64_t i = 0; i < n; i++) {
        uint64_t pos; void *p;
        while (!(p = zc_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_commit(&r, pos, SLOT_SIZE);
    }
    int stt = 0;
    waitpid(pid, &stt, 0);

    CHECK(atomic_load(&st[0]) == 0, "%llu wrong messages",
          (unsigned long long)atomic_load(&st[0]));
    /* Generous bound on purpose: the point is that blocking is rare, not that
     * it never happens. A descheduled consumer legitimately falls behind and
     * blocks a few times, and asserting zero would make this test a flake. */
    CHECK(atomic_load(&st[1]) < n / 20,
          "blocked %llu times in %llu messages — the ski-rental floor is not "
          "holding and fast arrivals are paying syscalls",
          (unsigned long long)atomic_load(&st[1]), (unsigned long long)n);

    munmap(st, sizeof(*st) * 2);
    zc_close(&r);
}

/* A bounded wait must return NULL on expiry rather than sleeping forever —
 * this is what lets a caller poll a shutdown flag between waits. */
static void test_notify_deadline(void)
{
    printf("adaptive notification: bounded wait expires instead of hanging\n");
    zc_ring_t r;
    CHECK(zc_create_notify(&r, 8, SLOT_SIZE) == 0, "zc_create_notify failed");

    zc_waiter_t w;
    zc_waiter_init(&w, ZC_BUDGET_SHIFT);
    uint64_t pos; uint32_t len;
    uint64_t t0 = zc_now_ns();
    void *p = zc_wait(&r, &w, &pos, &len, t0 + 20000000ULL);  /* 20 ms */
    uint64_t dt = zc_now_ns() - t0;
    CHECK(p == NULL, "bounded wait returned a message from an empty ring");
    CHECK(dt >= 20000000ULL, "returned after %lluns, before the deadline",
          (unsigned long long)dt);
    CHECK(dt < 2000000000ULL, "returned after %lluns, far past the deadline",
          (unsigned long long)dt);

    /* And it must leave no waiter registered behind, or the producer would
     * issue a wake syscall per message forever after. */
    CHECK(atomic_load(&r.ctrl->waiters) == 0,
          "waiter count leaked after a timed-out wait");
    zc_close(&r);
}

/* The lost-wakeup window, hammered directly.
 *
 * §9's argument is about a race whose window is a few instructions wide: the
 * producer reading ctrl->waiters at the same moment a consumer arms itself and
 * re-checks the ring. Nothing in the delivery tests above targets it — they
 * either arrive so slowly that the consumer is long asleep before the producer
 * publishes, or so fast that it never sleeps at all.
 *
 * This test aims squarely at the boundary by publishing at a gap comparable to
 * the learned spin budget, so the consumer is repeatedly deciding to sleep at
 * the instant the producer is deciding whether to wake it. A lost wakeup would
 * hang, so the wait is bounded and expiries are counted: the failure surfaces
 * as a number rather than as a test run that never returns.
 *
 * This exists because it is the empirical half of the argument. The formal
 * half is §9's modification-order reasoning, which `make tsan` can only check
 * because that reasoning was written with same-location RMWs rather than
 * fences — GCC's TSan does not instrument atomic_thread_fence at all. */

#define LW_MSGS 4000

static void test_notify_lost_wakeup_window(void)
{
    printf("adaptive notification: no lost wakeups at the arm/publish boundary\n");
    zc_ring_t r;
    CHECK(zc_create_notify(&r, 256, SLOT_SIZE) == 0, "zc_create_notify failed");

    /* [0] wrong, [1] timeouts, [2] blocks */
    _Atomic uint64_t *st = mmap(NULL, sizeof(*st) * 3, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    for (int i = 0; i < 3; i++) atomic_store(&st[i], 0);

    pid_t pid = fork();
    if (pid == 0) {
        zc_waiter_t w;
        zc_waiter_init(&w, ZC_BUDGET_SHIFT);
        for (uint64_t want = 0; want < LW_MSGS; want++) {
            uint64_t pos, got; uint32_t len;
            /* Generous relative to any real wake, tight enough that a genuine
             * lost wakeup cannot hide inside it. */
            void *p = zc_wait(&r, &w, &pos, &len, zc_now_ns() + 500000000ULL);
            if (!p) { atomic_fetch_add(&st[1], 1); break; }
            if (verify(p, &got) != 0 || got != want) atomic_fetch_add(&st[0], 1);
            zc_release(&r, pos);
        }
        atomic_store(&st[2], w.n_blocks);
        _exit(0);
    }

    /* Sweep the publish gap across the range the spin budget converges into,
     * so the arm/publish collision is hit from both sides rather than at one
     * fixed offset the policy could settle away from. */
    uint64_t next = zc_now_ns();
    for (uint64_t i = 0; i < LW_MSGS; i++) {
        next += 2000ULL + (i % 32) * 400ULL;    /* 2us .. 14.4us */
        while (zc_now_ns() < next) { }
        uint64_t pos; void *p;
        while (!(p = zc_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_commit(&r, pos, SLOT_SIZE);
    }
    int stt = 0;
    waitpid(pid, &stt, 0);

    CHECK(atomic_load(&st[1]) == 0,
          "%llu wait(s) expired — a wakeup was lost at the arm/publish boundary",
          (unsigned long long)atomic_load(&st[1]));
    CHECK(atomic_load(&st[0]) == 0, "%llu wrong messages",
          (unsigned long long)atomic_load(&st[0]));
    /* If the policy never slept, this test proved nothing about wakeups. */
    CHECK(atomic_load(&st[2]) > 0,
          "consumer never blocked, so the race window was never entered");

    munmap(st, sizeof(*st) * 3);
    zc_close(&r);
}

/* ---- §11: huge-page backing must be an optimisation, never a requirement ----
 *
 * The property under test is the fallback, not the fast path. Huge pages are
 * unavailable on most systems this will ever run on — hugetlbfs pools are
 * empty by default and embedded kernels often lack them entirely — so the
 * thing that must never break is creating a working ring anyway. These tests
 * therefore assert on the ring, and only report which backing it got.
 *
 * Deliberately not asserted: that ZC_HUGE_AUTO produces huge pages. That
 * depends on vm.nr_hugepages and on shmem_enabled, neither of which a unit
 * test may assume, and a test that fails on a stock machine would be
 * retrained into being ignored. */

static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

/* A slot count whose arena clears the ZC_HUGE_MIN_PAGES threshold, so AUTO
 * actually attempts a huge backing rather than declining on size. */
static uint32_t huge_slots(size_t *arena_out)
{
    size_t hp = zc_hugepage_size();
    if (!hp) hp = 2u << 20;
    uint32_t slots = 64;
    while ((size_t)slots * SLOT_SIZE < (size_t)ZC_HUGE_MIN_PAGES * hp)
        slots <<= 1;
    if (arena_out) *arena_out = (size_t)slots * SLOT_SIZE;
    return slots;
}

static void roundtrip(zc_ring_t *r, const char *what)
{
    uint64_t pos, id = 0; uint32_t len = 0;
    void *p = zc_reserve(r, &pos);
    CHECK(p != NULL, "%s: reserve failed", what);
    if (!p) return;
    fill(p, 7);
    zc_commit(r, pos, SLOT_SIZE);
    p = zc_acquire(r, &pos, &len);
    CHECK(p != NULL, "%s: acquire failed", what);
    if (!p) return;
    CHECK(verify(p, &id) == 0 && id == 7, "%s: payload corrupted", what);
    CHECK(len == SLOT_SIZE, "%s: len %u", what, len);
    zc_release(r, pos);
}

static void test_huge_fallback(void)
{
    printf("huge pages: auto degrades to a working ring whatever the system has\n");
    size_t arena = 0;
    uint32_t slots = huge_slots(&arena);
    zc_ring_t r;

    zc_set_hugepage_policy(ZC_HUGE_AUTO);
    CHECK(zc_create(&r, slots, SLOT_SIZE) == 0,
          "auto-policy create failed: %s", strerror(errno));
    if (failures) return;
    int b = zc_backing(&r);
    printf("    arena %zu MiB -> pages=%s\n", arena >> 20, zc_backing_name(b));
    /* Whatever it chose, the arena has to start on a boundary the choice can
     * actually use. A huge backing over a 4 KiB-aligned arena would be a
     * silent no-op — the bug this assert exists to catch. */
    if (b != ZC_BACKING_4K)
        CHECK((r.ctrl->arena_off & (zc_hugepage_size() - 1)) == 0,
              "arena_off %llu is not huge-page aligned under pages=%s",
              (unsigned long long)r.ctrl->arena_off, zc_backing_name(b));
    roundtrip(&r, "auto");
    zc_close(&r);

    /* OFF must reproduce the pre-§11 layout exactly: results/ was measured
     * against it and has to stay regenerable. */
    zc_set_hugepage_policy(ZC_HUGE_OFF);
    CHECK(zc_create(&r, slots, SLOT_SIZE) == 0, "off-policy create failed");
    if (failures) return;
    CHECK(zc_backing(&r) == ZC_BACKING_4K, "pages=4k requested, got %s",
          zc_backing_name(zc_backing(&r)));
    size_t pg = (size_t)sysconf(_SC_PAGESIZE);
    size_t slots_off = align_up(sizeof(zc_ctrl_t), ZC_CACHELINE);
    size_t legacy_off = align_up(slots_off + (size_t)slots * sizeof(zc_slot_t), pg);
    CHECK(r.ctrl->arena_off == legacy_off,
          "pages=4k moved the arena: arena_off=%llu, expected %zu",
          (unsigned long long)r.ctrl->arena_off, legacy_off);
    roundtrip(&r, "4k");
    zc_close(&r);

    /* A small ring must not take a huge page it cannot fill — the pool is a
     * scarce global resource (§11). */
    zc_set_hugepage_policy(ZC_HUGE_AUTO);
    CHECK(zc_create(&r, 8, SLOT_SIZE) == 0, "small-ring create failed");
    if (failures) return;
    CHECK(zc_backing(&r) == ZC_BACKING_4K,
          "a %u-byte arena took huge pages (%s)", 8u * SLOT_SIZE,
          zc_backing_name(zc_backing(&r)));
    zc_close(&r);
    zc_set_hugepage_policy(ZC_HUGE_AUTO);
}

static void test_huge_strict_and_attach(void)
{
    printf("huge pages: strict mode is honest, and peers agree on the backing\n");
    uint32_t slots = huge_slots(NULL);
    zc_ring_t r;

    /* REQUIRE either delivers hugetlb or fails. What it must never do is
     * succeed with 4 KiB pages, which would make an A/B measure its control
     * arm twice and report the result as a win. */
    zc_set_hugepage_policy(ZC_HUGE_REQUIRE);
    int strict_ok = (zc_create(&r, slots, SLOT_SIZE) == 0);
    if (!strict_ok) {
        printf("    pages=huge unavailable (%s) — skipping the strict arm\n",
               strerror(errno));
    } else {
        CHECK(zc_backing(&r) == ZC_BACKING_HUGETLB,
              "pages=huge succeeded but reports %s",
              zc_backing_name(zc_backing(&r)));
        roundtrip(&r, "huge");
        zc_close(&r);
    }

    /* A failed strict attempt must leave nothing behind. If it leaked the
     * hugetlbfs fd, a long sweep would exhaust the pool and later arms would
     * fall back without saying so. */
    zc_set_hugepage_policy(ZC_HUGE_AUTO);
    CHECK(zc_create(&r, slots, SLOT_SIZE) == 0,
          "create after a strict attempt failed: %s", strerror(errno));
    if (failures) return;

    /* The aligned-mapping path has its own way to be wrong: the creator maps
     * at a huge-aligned address through MAP_FIXED over a reservation, and a
     * peer repeats that independently. If either got it wrong, the child
     * reads the arena at the wrong offset — so check across a fork, where the
     * child re-derives everything from the control block rather than
     * inheriting the parent's pointers. */
    _Atomic uint32_t *bad = mmap(NULL, sizeof(*bad), PROT_READ | PROT_WRITE,
                                 MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    atomic_store(bad, 0);
    int fd = zc_fd(&r);
    pid_t pid = fork();
    if (pid == 0) {
        zc_ring_t peer;
        if (zc_attach(&peer, fd) != 0) _exit(2);
        if (zc_backing(&peer) != zc_backing(&r)) atomic_fetch_add(bad, 1);
        for (uint32_t i = 0; i < 1000; i++) {
            uint64_t pos, id; uint32_t len; void *p;
            while (!(p = zc_acquire(&peer, &pos, &len))) sched_yield();
            if (verify(p, &id) != 0 || id != i) atomic_fetch_add(bad, 1);
            zc_release(&peer, pos);
        }
        /* Not zc_close: it would close the inherited fd, which is the
         * parent's. Unmapping is the part under test anyway. */
        _exit(0);
    }
    for (uint32_t i = 0; i < 1000; i++) {
        uint64_t pos; void *p;
        while (!(p = zc_reserve(&r, &pos))) sched_yield();
        fill(p, i);
        zc_commit(&r, pos, SLOT_SIZE);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "peer exited %d (2 = zc_attach failed on a huge-aligned ring)",
          WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    CHECK(atomic_load(bad) == 0, "%u disagreements between creator and peer",
          atomic_load(bad));
    munmap(bad, sizeof(*bad));
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
    test_notify_unicast();
    test_notify_bcast();
    test_notify_fast_arrivals_do_not_block();
    test_notify_deadline();
    test_notify_lost_wakeup_window();
    test_huge_fallback();
    test_huge_strict_and_attach();

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nall checks passed\n");
    return 0;
}

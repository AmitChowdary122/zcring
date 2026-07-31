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

int main(void)
{
    test_basic();
    test_full();
    test_concurrent();
    test_cross_process();

    if (failures) { printf("\n%d check(s) FAILED\n", failures); return 1; }
    printf("\nall checks passed\n");
    return 0;
}

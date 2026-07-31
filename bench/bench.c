/*
 * bench — one-way latency harness, identical methodology across transports.
 *
 * Method: the producer stamps CLOCK_MONOTONIC into the first 8 bytes of the
 * payload; the consumer reads it and computes the delta. Same clock, same
 * machine, so the timestamps are directly comparable — no clock-sync fudge
 * and no RTT/2 assumption about symmetry.
 *
 * The comparison is deliberately like-for-like: every transport moves the
 * same number of payload bytes from the same producer to the same consumer.
 * The only thing that differs is how many times those bytes get copied.
 *
 * Known asymmetry, stated rather than hidden: the zcring consumer spins,
 * while pipe/unix consumers block in read(). Layer 2's adaptive
 * spin-then-futex path is what closes that gap honestly; until then, treat
 * zcring's idle-CPU cost as unrepresentative of the finished system.
 */
#define _GNU_SOURCE
#include "../src/zcring.h"

#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

enum { T_ZCRING, T_PIPE, T_UNIX };

/* --touch: make the application actually produce and consume every payload
 * byte, instead of only stamping and reading the 8-byte header.
 *
 * This matters more than it looks. Without it, zcring moves 8 real bytes and
 * merely asserts the rest exists, while pipe genuinely moves all of them —
 * which turns the headline graph into an artifact of the harness rather than
 * a property of the design. With it, both transports pay the same
 * application-inherent write pass and read pass, and the measured gap is only
 * the copies zcring actually eliminates. Always report --touch numbers as the
 * primary result. */
static volatile uint64_t g_sink;

static inline void produce_payload(uint8_t *p, uint32_t size, uint64_t seed)
{
    for (uint32_t i = 8; i < size; i += 64) p[i] = (uint8_t)(seed + i);
}

static inline void consume_payload(const uint8_t *p, uint32_t size)
{
    uint64_t s = 0;
    for (uint32_t i = 8; i < size; i += 64) s += p[i];
    g_sink += s;
}

static const char *t_name(int t)
{
    return t == T_ZCRING ? "zcring" : t == T_PIPE ? "pipe" : "unix";
}

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void pin(int cpu)
{
    if (cpu < 0) return;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0)
        fprintf(stderr, "warn: pin to cpu %d failed: %s\n", cpu, strerror(errno));
}

static void spin_until(uint64_t deadline)
{
    while (now_ns() < deadline) { /* busy-wait: sleep granularity is too coarse */ }
}

static int write_full(int fd, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; n -= (size_t)w;
    }
    return 0;
}

static int read_full(int fd, void *buf, size_t n)
{
    uint8_t *p = buf;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return -1;
        p += r; n -= (size_t)r;
    }
    return 0;
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static uint64_t pct(const uint64_t *sorted, size_t n, double p)
{
    if (!n) return 0;
    size_t i = (size_t)(p / 100.0 * (double)(n - 1) + 0.5);
    return sorted[i >= n ? n - 1 : i];
}

int main(int argc, char **argv)
{
    int      transport = T_ZCRING;
    uint32_t size      = 1024;
    uint32_t count     = 20000;
    uint32_t warmup    = 2000;
    uint32_t gap_us    = 50;
    int      cpu_prod = -1, cpu_cons = -1;
    int      csv = 0, touch = 0;

    for (int i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "--transport=", 12)) {
            const char *v = argv[i] + 12;
            transport = !strcmp(v, "pipe") ? T_PIPE
                      : !strcmp(v, "unix") ? T_UNIX : T_ZCRING;
        } else if (!strncmp(argv[i], "--size=", 7))   size   = (uint32_t)atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--count=", 8))    count  = (uint32_t)atoi(argv[i] + 8);
        else if (!strncmp(argv[i], "--warmup=", 9))   warmup = (uint32_t)atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--gap-us=", 9))   gap_us = (uint32_t)atoi(argv[i] + 9);
        else if (!strncmp(argv[i], "--cpu-prod=", 11)) cpu_prod = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--cpu-cons=", 11)) cpu_cons = atoi(argv[i] + 11);
        else if (!strcmp(argv[i], "--csv")) csv = 1;
        else if (!strcmp(argv[i], "--touch")) touch = 1;
        else if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [--transport=zcring|pipe|unix] [--size=N] [--count=N]\n"
                   "          [--warmup=N] [--gap-us=N] [--cpu-prod=N] [--cpu-cons=N]\n"
                   "          [--touch] [--csv]\n"
                   "  --touch  produce and consume every payload cache line (fair mode)\n",
                   argv[0]);
            return 0;
        }
    }
    if (size < 8) size = 8;

    uint32_t total = warmup + count;

    /* Samples live in a shared anonymous mapping so the consumer child can
     * record and the parent can report after reaping it. */
    size_t   samp_bytes = (size_t)total * sizeof(uint64_t);
    uint64_t *samples = mmap(NULL, samp_bytes, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (samples == MAP_FAILED) { perror("mmap samples"); return 1; }

    /* Ring sized to ~64 MiB of arena, clamped to a sane slot count. */
    uint32_t slots = 1024;
    while (slots > 8 && (uint64_t)slots * size > (64ULL << 20)) slots >>= 1;

    zc_ring_t ring;
    int fds[2] = { -1, -1 };

    if (transport == T_ZCRING) {
        if (zc_create(&ring, slots, size) != 0) { perror("zc_create"); return 1; }
    } else if (transport == T_PIPE) {
        if (pipe(fds) != 0) { perror("pipe"); return 1; }
    } else {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) { perror("socketpair"); return 1; }
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* ---------------- consumer ---------------- */
        pin(cpu_cons);
        if (transport == T_ZCRING) {
            for (uint32_t i = 0; i < total; i++) {
                uint64_t pos; uint32_t len; void *p;
                while (!(p = zc_acquire(&ring, &pos, &len))) { /* spin */ }
                uint64_t sent;
                memcpy(&sent, p, sizeof(sent));
                if (touch) consume_payload(p, size);
                samples[i] = now_ns() - sent;
                zc_release(&ring, pos);
            }
        } else {
            close(fds[1]);
            uint8_t *buf = malloc(size);
            for (uint32_t i = 0; i < total; i++) {
                if (read_full(fds[0], buf, size) != 0) { perror("read"); _exit(1); }
                uint64_t sent;
                memcpy(&sent, buf, sizeof(sent));
                if (touch) consume_payload(buf, size);
                samples[i] = now_ns() - sent;
            }
            free(buf);
        }
        _exit(0);
    }

    /* ---------------- producer ---------------- */
    pin(cpu_prod);
    uint64_t gap = (uint64_t)gap_us * 1000ULL;
    uint64_t next = now_ns();

    if (transport == T_ZCRING) {
        for (uint32_t i = 0; i < total; i++) {
            next += gap;
            spin_until(next);
            uint64_t pos; void *p;
            while (!(p = zc_reserve(&ring, &pos))) { /* ring full: spin */ }
            /* Construct in place. The stamp IS the message header — there is
             * no staging buffer and no copy. */
            uint64_t t = now_ns();
            memcpy(p, &t, sizeof(t));
            if (touch) produce_payload(p, size, i); /* straight into the slot */
            zc_commit(&ring, pos, size);
        }
    } else {
        close(fds[0]);
        uint8_t *buf = calloc(1, size);
        for (uint32_t i = 0; i < total; i++) {
            next += gap;
            spin_until(next);
            uint64_t t = now_ns();
            memcpy(buf, &t, sizeof(t));
            if (touch) produce_payload(buf, size, i); /* into a staging buffer */
            if (write_full(fds[1], buf, size) != 0) { perror("write"); return 1; }
        }
        free(buf);
    }

    int status = 0;
    waitpid(pid, &status, 0);

    /* Discard warmup: page faults and cold caches belong to setup, not to
     * steady-state latency. */
    uint64_t *s = samples + warmup;
    qsort(s, count, sizeof(uint64_t), cmp_u64);

    double mean = 0;
    for (uint32_t i = 0; i < count; i++) mean += (double)s[i];
    mean /= (double)count;

    if (csv) {
        printf("%s,%u,%.0f,%llu,%llu,%llu,%llu,%llu\n", t_name(transport), size, mean,
               (unsigned long long)pct(s, count, 50),
               (unsigned long long)pct(s, count, 99),
               (unsigned long long)pct(s, count, 99.9),
               (unsigned long long)pct(s, count, 99.99),
               (unsigned long long)s[count - 1]);
    } else {
        printf("transport=%-7s size=%-8u n=%u\n", t_name(transport), size, count);
        printf("  mean   %10.0f ns\n", mean);
        printf("  p50    %10llu ns\n", (unsigned long long)pct(s, count, 50));
        printf("  p99    %10llu ns\n", (unsigned long long)pct(s, count, 99));
        printf("  p99.9  %10llu ns\n", (unsigned long long)pct(s, count, 99.9));
        printf("  p99.99 %10llu ns\n", (unsigned long long)pct(s, count, 99.99));
        printf("  max    %10llu ns\n", (unsigned long long)s[count - 1]);
    }

    if (transport == T_ZCRING) zc_close(&ring);
    munmap(samples, samp_bytes);
    return 0;
}

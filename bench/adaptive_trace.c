/*
 * adaptive_trace — emits a CSV time series of the learned spin-then-futex
 * state (src/zcring.h §§5-10) while the producer's offered inter-arrival
 * gap changes mid-run, in three phases (default 200 µs -> 20 µs -> 2 ms).
 *
 * This is not a latency benchmark. The number it produces is a policy
 * *shape* -- does the learned spin budget track a changing arrival process
 * -- not an absolute latency this project has committed to reproducing on
 * one specific CPU, so none of --touch, thermal gating, or the i3 dataset
 * guard apply here. See scripts/adaptive_trace.sh for why that script does
 * not call check_machine().
 *
 * One producer thread, one consumer thread, one process: zcring's shared
 * memory and futex path work identically within a process as across two,
 * and staying single-process keeps this tool free of the fork/CPU-pinning
 * machinery bench.c needs for a real latency measurement, none of which
 * this trace needs.
 *
 * usage: adaptive_trace [--n1=N] [--n2=N] [--n3=N]
 *                        [--gap1=US] [--gap2=US] [--gap3=US]
 *
 * Prints CSV to stdout:
 *   t_ms,phase_us,seq,spin_ns,gap_ewma_ns,wake_ewma_ns,burn_ewma_ns,blocked
 *
 * t_ms       elapsed time since the consumer loop started
 * phase_us   the current phase's nominal producer gap (200 / 20 / 2000)
 * seq        0-based message index
 * spin_ns    w.spin_ns after this wait -- the learned control variable S
 * gap_ewma_ns / wake_ewma_ns / burn_ewma_ns -- the three EWMAs §6 tracks
 * blocked    1 if this wait actually parked in FUTEX_WAIT, 0 if the
 *            message was caught while still spinning
 */
#include "../src/zcring.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    zc_ring_t *ring;
    uint32_t   counts[3];
    uint32_t   gaps_us[3];
} prod_args_t;

static void sleep_us(uint32_t us)
{
    struct timespec ts;
    ts.tv_sec  = us / 1000000u;
    ts.tv_nsec = (long)(us % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
}

static void *producer_main(void *arg)
{
    prod_args_t *a = arg;
    for (int ph = 0; ph < 3; ph++) {
        for (uint32_t i = 0; i < a->counts[ph]; i++) {
            uint64_t pos;
            void *p;
            do { p = zc_reserve(a->ring, &pos); } while (!p);
            zc_commit(a->ring, pos, 0);
            sleep_us(a->gaps_us[ph]);
        }
    }
    return NULL;
}

int main(int argc, char **argv)
{
    uint32_t n1 = 800, n2 = 800, n3 = 400;
    uint32_t gap1 = 200, gap2 = 20, gap3 = 2000; /* microseconds */

    for (int i = 1; i < argc; i++) {
        if      (!strncmp(argv[i], "--n1=", 5))   n1   = (uint32_t)atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "--n2=", 5))   n2   = (uint32_t)atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "--n3=", 5))   n3   = (uint32_t)atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "--gap1=", 7)) gap1 = (uint32_t)atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--gap2=", 7)) gap2 = (uint32_t)atoi(argv[i] + 7);
        else if (!strncmp(argv[i], "--gap3=", 7)) gap3 = (uint32_t)atoi(argv[i] + 7);
        else {
            fprintf(stderr,
                    "usage: %s [--n1=N] [--n2=N] [--n3=N] "
                    "[--gap1=US] [--gap2=US] [--gap3=US]\n", argv[0]);
            return 1;
        }
    }

    zc_ring_t ring;
    if (zc_create_notify(&ring, 1024, 64) != 0) {
        fprintf(stderr, "zc_create_notify failed\n");
        return 1;
    }

    prod_args_t pa = {
        .ring     = &ring,
        .counts   = { n1, n2, n3 },
        .gaps_us  = { gap1, gap2, gap3 },
    };
    pthread_t prod_thread;
    if (pthread_create(&prod_thread, NULL, producer_main, &pa) != 0) {
        fprintf(stderr, "pthread_create failed\n");
        return 1;
    }

    zc_waiter_t w;
    zc_waiter_init(&w, ZC_BUDGET_SHIFT);

    printf("t_ms,phase_us,seq,spin_ns,gap_ewma_ns,wake_ewma_ns,burn_ewma_ns,blocked\n");

    uint64_t t_start = zc_now_ns();
    uint32_t seq = 0;
    for (int ph = 0; ph < 3; ph++) {
        for (uint32_t i = 0; i < pa.counts[ph]; i++) {
            uint64_t prev_blocks = w.n_blocks;
            uint64_t pos = 0;
            uint32_t len;
            zc_wait(&ring, &w, &pos, &len, 0); /* deadline 0: waits forever */
            zc_release(&ring, pos);

            uint64_t now = zc_now_ns();
            int blocked = (w.n_blocks != prev_blocks) ? 1 : 0;
            printf("%.3f,%u,%u,%llu,%llu,%llu,%llu,%d\n",
                   (double)(now - t_start) / 1e6,
                   pa.gaps_us[ph], seq,
                   (unsigned long long)w.spin_ns,
                   (unsigned long long)w.gap_ewma,
                   (unsigned long long)w.wake_ewma,
                   (unsigned long long)w.burn_ewma,
                   blocked);
            seq++;
        }
    }

    pthread_join(prod_thread, NULL);
    zc_close(&ring);
    return 0;
}

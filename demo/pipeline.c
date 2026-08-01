/*
 * pipeline — camera -> {edge-count, jitter-tracker, checksum} fan-out demo.
 *
 * One producer synthesises 640x480 grayscale frames at 30 fps and delivers
 * every frame to three consumers doing three different, real jobs. Two
 * transports implement the identical pipeline:
 *
 *   zcring   one zc_bcast_commit() per frame. All three consumers read the
 *            same bytes out of the same slot. Copies: zero, independent of
 *            consumer count.
 *   unix     three separate AF_UNIX stream sockets, one per consumer. The
 *            producer writes the frame to each. Copies: one staging write,
 *            then a kernel copy in and a kernel copy out per socket.
 *
 * The three consumer functions (process_edges, process_checksum,
 * process_jitter_scan) are called identically by both transports' consumer
 * loops -- same code, same input pointer type, same touch pattern. The only
 * thing that differs between --transport=zcring and --transport=unix is how
 * the bytes get from the producer's frame buffer into the pointer those
 * functions are called with. That is the property this demo exists to show,
 * not a claim to take on faith.
 *
 * Frame layout (FRAME_SIZE bytes total):
 *   [0..8)   producer_ts_ns   CLOCK_MONOTONIC send time -- for latency
 *   [8..16)  frame_seq        monotonic per-tick counter -- for drop detection
 *   [16..)   FRAME_W * FRAME_H grayscale pixels
 *
 * frame_seq increments every 30fps tick whether or not the tick's frame was
 * actually published (see "drop policy" below), so a consumer that sees
 * frame_seq jump by more than 1 knows unambiguously how many frames were
 * dropped, regardless of transport.
 *
 * Drop policy: a camera cannot be told to pause, so the producer never
 * blocks waiting for a slow consumer. zcring: zc_bcast_reserve() is given a
 * bounded number of retries (~1.5 frame periods); if the slowest consumer is
 * still not caught up, this tick is skipped. unix: poll() with the same
 * bound checks all three sockets are writable before committing to write();
 * if not, this tick is skipped for all three, keeping "which frames every
 * consumer saw" identical between the two transports so the comparison
 * stays like-for-like. At 30fps this should not fire in normal operation --
 * that is the expected, honest result, not a test that was skipped.
 *
 * CPU pinning: producer + 3 consumers is 4 roles on a machine with 2
 * physical cores (see RUNNING.md / STATUS.md Open problem #3) -- one
 * consumer necessarily shares a physical core with either the producer or
 * another consumer. scripts/demo.sh computes and passes the assignment;
 * see its comments for which consumer draws the short straw and why it
 * doesn't matter much at a 33ms period.
 */
#define _GNU_SOURCE
#include "../src/zcring.h"

#include <errno.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME_W        640
#define FRAME_H        480
#define FRAME_PIXELS   ((size_t)FRAME_W * (size_t)FRAME_H)
#define HDR_BYTES      16
#define FRAME_SIZE     (HDR_BYTES + FRAME_PIXELS)
#define NCONS          3
#define FPS            30
#define PERIOD_NS      (1000000000ULL / FPS)
#define DROP_BOUND_NS  (PERIOD_NS + PERIOD_NS / 2)  /* ~1.5 frame periods */
#define HIST_SAMPLES   300     /* ~10s at 30fps -- fixed, never grows */
#define REDRAW_NS      1000000000ULL

/* Memory-passes accounting, same methodology as README's "passes over
 * memory" table: zcring pays 1 producer write + N consumer reads; unix pays
 * 1 staging write + N * (kernel-in + kernel-out + consumer read). Avoided =
 * 2 passes per consumer, i.e. 2*NCONS frame-equivalents per published frame. */
#define PASSES_AVOIDED_PER_FRAME (2 * NCONS)

enum { CONS_EDGE = 0, CONS_JITTER = 1, CONS_CHECKSUM = 2 };

static const char *cons_name(int i)
{
    return i == CONS_EDGE ? "edge-count" : i == CONS_JITTER ? "jitter    " : "checksum  ";
}

typedef struct {
    _Atomic uint64_t hist[HIST_SAMPLES]; /* recent one-way latencies, ns */
    _Atomic uint64_t hist_idx;           /* monotonic write counter */
    _Atomic uint64_t frames_seen;
    _Atomic uint64_t drops;
    _Atomic uint64_t last_result;        /* cosmetic: edges / checksum / range */
    _Atomic uint32_t started;
} cons_stats_t;

typedef struct {
    _Atomic uint32_t ready;
    _Atomic uint64_t frames_published;
    _Atomic uint64_t producer_drops;
    _Atomic uint64_t bytes_avoided;
    cons_stats_t cons[NCONS];
} shared_stats_t;

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

static inline uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void sleep_until(uint64_t deadline_ns)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(deadline_ns / 1000000000ULL);
    ts.tv_nsec = (long)(deadline_ns % 1000000000ULL);
    /* TIMER_ABSTIME against a fixed schedule (start + n*period), not a
     * relative sleep re-issued each tick -- the latter accumulates drift
     * from its own wake/dispatch overhead over a 10-minute run, this does
     * not: a late wake shortens the next sleep, it never compounds. */
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
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

/* ---------------------------------------------------------------------
 * Application work. Called identically by both transports' consumer
 * loops -- this is what "identical application work" means in practice.
 * All three touch every pixel, so neither transport gets to look better
 * by having a consumer that conveniently ignores most of the frame.
 * --------------------------------------------------------------------- */

static uint64_t process_edges(const uint8_t *px)
{
    uint64_t edges = 0;
    for (int y = 0; y < FRAME_H; y++) {
        const uint8_t *row = px + (size_t)y * FRAME_W;
        for (int x = 1; x < FRAME_W; x++) {
            int d = (int)row[x] - (int)row[x - 1];
            if (d < 0) d = -d;
            if (d > 24) edges++;
        }
    }
    return edges;
}

static uint64_t process_checksum(const uint8_t *px)
{
    /* FNV-1a: cheap, real, touches every byte, not a stub loop. */
    uint64_t c = 1469598103934665603ULL;
    for (size_t i = 0; i < FRAME_PIXELS; i++) {
        c ^= px[i];
        c *= 1099511628211ULL;
    }
    return c;
}

static uint64_t process_jitter_scan(const uint8_t *px)
{
    /* Full-frame touch component of the jitter-tracker: signal range, so
     * this consumer's per-frame cost is comparable to the other two rather
     * than "just read 16 header bytes", which would understate the
     * transport's real cost for a consumer that does real work. */
    uint8_t lo = 255, hi = 0;
    for (size_t i = 0; i < FRAME_PIXELS; i++) {
        if (px[i] < lo) lo = px[i];
        if (px[i] > hi) hi = px[i];
    }
    return (uint64_t)(hi - lo);
}

static void synth_frame(uint8_t *px, uint64_t seq)
{
    for (int y = 0; y < FRAME_H; y++) {
        uint8_t *row = px + (size_t)y * FRAME_W;
        for (int x = 0; x < FRAME_W; x++)
            row[x] = (uint8_t)(x + y + seq * 2);
    }
}

/* ---------------------------------------------------------------------
 * Consumer-side bookkeeping shared by both transports.
 * --------------------------------------------------------------------- */

static void record_frame(cons_stats_t *cs, const uint8_t *frame,
                          uint64_t *expected_seq, int id)
{
    uint64_t send_ts, seq;
    memcpy(&send_ts, frame, 8);
    memcpy(&seq, frame + 8, 8);

    if (atomic_load_explicit(&cs->frames_seen, memory_order_relaxed) == 0) {
        *expected_seq = seq; /* first frame this consumer sees: joined here */
    } else if (seq != *expected_seq) {
        uint64_t missed = seq - *expected_seq;
        atomic_fetch_add_explicit(&cs->drops, missed, memory_order_relaxed);
    }
    *expected_seq = seq + 1;

    uint64_t result = id == CONS_EDGE     ? process_edges(frame + HDR_BYTES)
                     : id == CONS_CHECKSUM ? process_checksum(frame + HDR_BYTES)
                                            : process_jitter_scan(frame + HDR_BYTES);
    atomic_store_explicit(&cs->last_result, result, memory_order_relaxed);

    uint64_t latency = now_ns() - send_ts;
    uint64_t idx = atomic_fetch_add_explicit(&cs->hist_idx, 1, memory_order_relaxed);
    atomic_store_explicit(&cs->hist[idx % HIST_SAMPLES], latency, memory_order_relaxed);
    atomic_fetch_add_explicit(&cs->frames_seen, 1, memory_order_relaxed);
}

/* ---------------------------------------------------------------------
 * Dashboard -- large numbers, few of them, redrawn in place.
 * --------------------------------------------------------------------- */

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void percentiles(cons_stats_t *cs, uint64_t *p50, uint64_t *p99)
{
    uint64_t buf[HIST_SAMPLES];
    uint64_t idx = atomic_load_explicit(&cs->hist_idx, memory_order_relaxed);
    size_t n = idx < HIST_SAMPLES ? (size_t)idx : HIST_SAMPLES;
    if (n == 0) { *p50 = *p99 = 0; return; }
    for (size_t i = 0; i < n; i++)
        buf[i] = atomic_load_explicit(&cs->hist[i], memory_order_relaxed);
    qsort(buf, n, sizeof(uint64_t), cmp_u64);
    *p50 = buf[(size_t)(0.50 * (double)(n - 1) + 0.5)];
    *p99 = buf[(size_t)(0.99 * (double)(n - 1) + 0.5)];
}

static void fmt_us(char *out, size_t outsz, uint64_t ns)
{
    snprintf(out, outsz, "%.0f us", (double)ns / 1000.0);
}

static void draw(shared_stats_t *st, int transport_zcring, uint64_t start,
                  uint64_t elapsed_ns, uint32_t nc_ready)
{
    uint64_t frames = atomic_load_explicit(&st->frames_published, memory_order_relaxed);
    uint64_t pdrops = atomic_load_explicit(&st->producer_drops, memory_order_relaxed);
    uint64_t avoided = atomic_load_explicit(&st->bytes_avoided, memory_order_relaxed);
    uint64_t secs = elapsed_ns / 1000000000ULL;

    printf("\033[H"); /* home, no clear -- avoids flicker on a redraw-in-place */
    printf("\033[1m  zcring FAN-OUT DEMO  --  camera -> {edge-count, jitter, checksum}\033[0m\n");
    printf("  transport: \033[1m%s\033[0m%s\n",
           transport_zcring ? "ZCRING (broadcast, zero-copy)" : "UNIX SOCKETS (3x, comparator)",
           nc_ready < NCONS ? "   [starting up...]" : "                              ");
    printf("  ================================================================\n\n");
    printf("  frames sent   \033[1m%8llu\033[0m     elapsed   %02llu:%02llu:%02llu     drops(producer) %llu\n\n",
           (unsigned long long)frames,
           (unsigned long long)(secs / 3600), (unsigned long long)((secs / 60) % 60),
           (unsigned long long)(secs % 60), (unsigned long long)pdrops);

    printf("  ----------------------------------------------------------------\n");
    printf("  consumer          p50         p99         frames        drops\n");
    printf("  ----------------------------------------------------------------\n");
    for (int i = 0; i < NCONS; i++) {
        uint64_t p50, p99;
        percentiles(&st->cons[i], &p50, &p99);
        char b50[16], b99[16];
        fmt_us(b50, sizeof(b50), p50);
        fmt_us(b99, sizeof(b99), p99);
        printf("  %s   \033[1m%8s\033[0m   \033[1m%8s\033[0m     %8llu     %6llu\n",
               cons_name(i), b50, b99,
               (unsigned long long)atomic_load_explicit(&st->cons[i].frames_seen, memory_order_relaxed),
               (unsigned long long)atomic_load_explicit(&st->cons[i].drops, memory_order_relaxed));
    }
    printf("  ----------------------------------------------------------------\n\n");

    if (transport_zcring) {
        double gb = (double)avoided / (1024.0 * 1024.0 * 1024.0);
        printf("  memory traffic avoided vs sockets:   \033[1m%8.2f GB\033[0m\n", gb);
    } else {
        printf("  memory traffic avoided vs sockets:   (this IS the socket baseline)\n");
    }
    printf("                                                                    \n");
    fflush(stdout);
    (void)start;
}

/* ---------------------------------------------------------------------
 * main
 * --------------------------------------------------------------------- */

int main(int argc, char **argv)
{
    int transport_zcring = 1;
    int duration_s = 600;
    int cpu_prod = -1;
    int cpu_cons[NCONS] = { -1, -1, -1 };

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--transport=unix")) transport_zcring = 0;
        else if (!strcmp(argv[i], "--transport=zcring")) transport_zcring = 1;
        else if (!strncmp(argv[i], "--duration=", 11)) duration_s = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--cpu-prod=", 11)) cpu_prod = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--cpu-cons=", 11)) {
            const char *v = argv[i] + 11;
            for (int k = 0; k < NCONS && *v; k++) {
                cpu_cons[k] = atoi(v);
                while (*v && *v != ',') v++;
                if (*v == ',') v++;
            }
        } else if (!strcmp(argv[i], "--help")) {
            printf("usage: %s [--transport=zcring|unix] [--duration=SECONDS]\n"
                   "          [--cpu-prod=N] [--cpu-cons=A,B,C]\n", argv[0]);
            return 0;
        }
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);
    signal(SIGPIPE, SIG_IGN); /* a closed stdout (e.g. piped to `head`) must
                                * not silently kill the producer mid-run and
                                * orphan the consumers -- let printf/fflush
                                * fail and keep going rather than die. */

    shared_stats_t *st = mmap(NULL, sizeof(*st), PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (st == MAP_FAILED) { perror("mmap stats"); return 1; }
    memset(st, 0, sizeof(*st));

    zc_ring_t ring;
    int fds[NCONS][2];
    for (int k = 0; k < NCONS; k++) { fds[k][0] = -1; fds[k][1] = -1; }

    if (transport_zcring) {
        /* 512 slots * FRAME_SIZE (~307 KiB) ~= 157 MiB of buffering, ~17s at
         * 30fps -- generous headroom so the drop path is a real safety net,
         * not the expected behaviour. */
        if (zc_create_bcast(&ring, 512, (uint32_t)FRAME_SIZE) != 0) {
            perror("zc_create_bcast"); return 1;
        }
    } else {
        for (int k = 0; k < NCONS; k++) {
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds[k]) != 0) {
                perror("socketpair"); return 1;
            }
        }
    }

    pid_t kids[NCONS];
    for (int k = 0; k < NCONS; k++) {
        kids[k] = fork();
        if (kids[k] < 0) { perror("fork"); return 1; }
        if (kids[k] != 0) continue;

        /* ---------------- consumer k ---------------- */
        pin(cpu_cons[k]);
        cons_stats_t *cs = &st->cons[k];
        uint64_t expected_seq = 0;
        /* Captured immediately after fork, before any reparenting can have
         * happened. Comparing against THIS rather than against a fixed "1"
         * is what makes the orphan watchdog below correct in environments
         * with a child-subreaper (containers, systemd --user, this very
         * sandbox) that catch orphans before init ever sees them -- PPID
         * changing to *anything else* is the actual signal, not PPID
         * becoming exactly 1. */
        pid_t orig_parent = getppid();

        if (transport_zcring) {
            int id = zc_bcast_join(&ring);
            if (id < 0) { perror("zc_bcast_join"); _exit(1); }
            atomic_store_explicit(&cs->started, 1, memory_order_release);
            atomic_fetch_add_explicit(&st->ready, 1, memory_order_release);

            uint32_t spins = 0;
            while (!g_stop) {
                uint64_t pos; uint32_t len; void *p;
                while (!(p = zc_bcast_acquire(&ring, id, &pos, &len))) {
                    if (g_stop) goto zcring_done;
                    if (++spins >= 2000) {
                        spins = 0;
                        /* Orphan watchdog: a duration-elapsed exit sends
                         * SIGTERM explicitly, but if the producer dies any
                         * other way (killed, crashed, terminal closed) this
                         * process reparents to init and would otherwise spin
                         * on an empty ring forever -- a real leak on a tool
                         * meant to run unattended for ten minutes. getppid()
                         * is cheap enough to check once per yield, not once
                         * per spin. */
                        if (getppid() != orig_parent) goto zcring_done;
                        sched_yield();
                    }
                }
                record_frame(cs, (const uint8_t *)p, &expected_seq, k);
                zc_bcast_release(&ring, id, pos);
            }
        zcring_done:
            zc_bcast_leave(&ring, id);
        } else {
            close(fds[k][1]);
            for (int j = 0; j < NCONS; j++) if (j != k) { close(fds[j][0]); close(fds[j][1]); }
            uint8_t *buf = malloc(FRAME_SIZE);
            atomic_store_explicit(&cs->started, 1, memory_order_release);
            atomic_fetch_add_explicit(&st->ready, 1, memory_order_release);

            while (!g_stop) {
                size_t got = 0;
                while (got < FRAME_SIZE) {
                    ssize_t r = read(fds[k][0], buf + got, FRAME_SIZE - got);
                    if (r < 0) { if (errno == EINTR) continue; goto out; }
                    if (r == 0) goto out; /* producer closed: shutting down */
                    got += (size_t)r;
                }
                record_frame(cs, buf, &expected_seq, k);
            }
        out:
            free(buf);
        }
        _exit(0);
    }

    /* ---------------- producer ---------------- */
    pin(cpu_prod);
    if (!transport_zcring)
        for (int k = 0; k < NCONS; k++) close(fds[k][0]);

    while (atomic_load_explicit(&st->ready, memory_order_acquire) < NCONS) sched_yield();

    uint8_t *frame = malloc(FRAME_SIZE);
    struct pollfd pfds[NCONS];
    if (!transport_zcring)
        for (int k = 0; k < NCONS; k++) { pfds[k].fd = fds[k][1]; pfds[k].events = POLLOUT; }

    uint64_t start = now_ns();
    uint64_t next = start;
    uint64_t last_draw = start;
    uint64_t seq = 0;
    uint64_t run_deadline = start + (uint64_t)duration_s * 1000000000ULL;

    while (!g_stop && now_ns() < run_deadline) {
        next += PERIOD_NS;
        sleep_until(next);

        uint64_t ts = now_ns();
        memcpy(frame, &ts, 8);
        memcpy(frame + 8, &seq, 8);
        synth_frame(frame + HDR_BYTES, seq);
        seq++;

        int published;
        if (transport_zcring) {
            uint64_t pos; void *p = NULL;
            uint64_t give_up = now_ns() + DROP_BOUND_NS;
            uint32_t spins = 0;
            for (;;) {
                p = zc_bcast_reserve(&ring, &pos);
                if (p) break;
                if (now_ns() >= give_up) break;
                if (++spins >= 2000) { spins = 0; sched_yield(); }
            }
            if (p) {
                memcpy(p, frame, FRAME_SIZE);
                zc_bcast_commit(&ring, pos, (uint32_t)FRAME_SIZE);
                published = 1;
            } else {
                published = 0;
            }
        } else {
            int wait_ms = (int)(DROP_BOUND_NS / 1000000ULL);
            int rc = poll(pfds, NCONS, wait_ms);
            int all_ready = rc == NCONS;
            if (rc > 0 && !all_ready) {
                all_ready = 1;
                for (int k = 0; k < NCONS; k++)
                    if (!(pfds[k].revents & POLLOUT)) all_ready = 0;
            }
            if (all_ready) {
                for (int k = 0; k < NCONS; k++) {
                    size_t sent = 0;
                    while (sent < FRAME_SIZE) {
                        ssize_t w = write(fds[k][1], frame + sent, FRAME_SIZE - sent);
                        if (w < 0) { if (errno == EINTR) continue; break; }
                        sent += (size_t)w;
                    }
                }
                published = 1;
            } else {
                published = 0;
            }
        }

        if (published) {
            atomic_fetch_add_explicit(&st->frames_published, 1, memory_order_relaxed);
            if (transport_zcring)
                atomic_fetch_add_explicit(&st->bytes_avoided,
                    (uint64_t)PASSES_AVOIDED_PER_FRAME * FRAME_SIZE, memory_order_relaxed);
        } else {
            atomic_fetch_add_explicit(&st->producer_drops, 1, memory_order_relaxed);
        }

        uint64_t nowt = now_ns();
        if (nowt - last_draw >= REDRAW_NS) {
            draw(st, transport_zcring, start, nowt - start, NCONS);
            last_draw = nowt;
        }
    }

    g_stop = 1;
    /* g_stop is per-process: a duration-elapsed exit (no signal involved)
     * never sets the children's own copies, so they must be told
     * explicitly or they hang forever spinning/blocking for a frame that
     * will never come. SIGTERM covers that case; an interactive Ctrl-C
     * covers itself, since the terminal delivers SIGINT to the whole
     * foreground process group (parent and children alike) already. */
    for (int k = 0; k < NCONS; k++) kill(kids[k], SIGTERM);
    if (!transport_zcring)
        for (int k = 0; k < NCONS; k++) close(fds[k][1]);

    int failed = 0;
    for (int k = 0; k < NCONS; k++) {
        int status = 0;
        waitpid(kids[k], &status, 0);
        if (!WIFEXITED(status)) failed = 1;
    }

    draw(st, transport_zcring, start, now_ns() - start, NCONS);
    printf("\n  stopped after %llu frames published, %llu producer-side drops (%llu ticks total).\n",
           (unsigned long long)atomic_load_explicit(&st->frames_published, memory_order_relaxed),
           (unsigned long long)atomic_load_explicit(&st->producer_drops, memory_order_relaxed),
           (unsigned long long)seq);

    free(frame);
    if (transport_zcring) zc_close(&ring);
    return failed;
}

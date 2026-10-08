/*
 * malicious_consumer — a consumer that has turned hostile.
 *
 * It attaches to a zcring ring exactly the way a legitimate consumer does,
 * through the fds the producer handed it, and then tries to corrupt what every
 * other peer is reading. It is a separate executable on purpose: it holds
 * nothing inherited from the producer, only what an unrelated process that
 * was given access to the ring would hold.
 *
 * The attack, in order:
 *
 *   1. Try every route this process has to a writable view of the arena:
 *      mprotect() its own mapping up to write; mmap() the arena fd writable;
 *      reopen the fd through /proc/self/fd with O_RDWR and mmap that; find the
 *      producer's own writable mapping through /proc/<producer>/maps and write
 *      through /proc/<producer>/mem. Each result is reported as it happens.
 *   2. Write 0xDEADBEEF across the whole arena, as fast as it can, until it is
 *      stopped.
 *
 * Nothing here knows whether the ring is sealed. The only difference between
 * the two runs of attack_demo is a flag the *producer* takes; this binary,
 * and this attack, are identical in both. On an unsealed ring every route is
 * open and step 2 corrupts every consumer. On a sealed ring every route is
 * refused by the kernel and the first store of step 2 takes SIGSEGV.
 *
 * What it deliberately does not try: the control block. In Phase 1 that is
 * still writable by every consumer and a hostile one can stall or confuse the
 * ring through it — see zcring.h §14. Corrupting it here would make the demo
 * look worse for the unsealed run and say nothing about the arena, which is
 * the property under test.
 *
 * Usage (attack_demo runs this; you normally do not):
 *   malicious_consumer --fd=N [--arena-fd=M] [--producer-pid=P] [--ready-fd=K]
 */
#define _GNU_SOURCE
#include "../src/zcring.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>

#define TAG "[attacker]  "

static void report(const char *what, int granted, int err)
{
    if (granted)
        printf(TAG "%-46s -> GRANTED\n", what);
    else
        printf(TAG "%-46s -> denied (%s)\n", what, strerror(err));
}

/* The producer's writable mapping of the arena, found through
 * /proc/<pid>/maps and written through /proc/<pid>/mem. This is the route a
 * same-user attacker has around any per-mapping protection, and the reason
 * attack_demo makes the producer non-dumpable — see zcring.h §14. */
static void try_producer_mem(pid_t prod)
{
    char path[64], line[512];
    snprintf(path, sizeof path, "/proc/%d/maps", (int)prod);
    FILE *f = fopen(path, "re");
    if (!f) { report("find producer's mapping via /proc/<pid>/maps", 0, errno); return; }

    unsigned long lo = 0, hi = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long a, b;
        char perms[8];
        if (sscanf(line, "%lx-%lx %7s", &a, &b, perms) == 3 &&
            perms[1] == 'w' && perms[3] == 's' && strstr(line, "zcring")) {
            lo = a; hi = b;   /* last one wins: the arena follows the ctrl block */
        }
    }
    fclose(f);
    if (!lo) { report("find producer's mapping via /proc/<pid>/maps", 0, ENOENT); return; }

    snprintf(path, sizeof path, "/proc/%d/mem", (int)prod);
    int mem = open(path, O_RDWR | O_CLOEXEC);
    if (mem < 0) { report("write through /proc/<producer>/mem", 0, errno); return; }
    uint32_t junk = 0xDEADBEEFu;
    int ok = pwrite(mem, &junk, sizeof junk, (off_t)(lo + (hi - lo) / 2)) ==
             (ssize_t)sizeof junk;
    report("write through /proc/<producer>/mem", ok, errno);
    close(mem);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int fd = -1, afd = -1, ready = -1;
    pid_t prod = 0;
    for (int i = 1; i < argc; i++) {
        if      (!strncmp(argv[i], "--fd=", 5))           fd    = atoi(argv[i] + 5);
        else if (!strncmp(argv[i], "--arena-fd=", 11))    afd   = atoi(argv[i] + 11);
        else if (!strncmp(argv[i], "--producer-pid=", 15)) prod = (pid_t)atoi(argv[i] + 15);
        else if (!strncmp(argv[i], "--ready-fd=", 11))    ready = atoi(argv[i] + 11);
    }
    if (fd < 0) { fprintf(stderr, "usage: %s --fd=N [--arena-fd=M]\n", argv[0]); return 2; }

    /* Attach the way any consumer would. A sealed ring hands out two fds and
     * needs zc_attach_sealed(); that is the API telling it apart, not the
     * attacker choosing to be gentle. */
    zc_ring_t r;
    int rc = afd >= 0 ? zc_attach_sealed(&r, fd, afd) : zc_attach(&r, fd);
    if (rc != 0) { perror(TAG "attach"); return 2; }

    uint8_t *arena = r.arena;
    size_t   alen  = (size_t)r.slot_size * (r.mask + 1u);
    int      target = afd >= 0 ? afd : fd;   /* the file the arena lives in */
    off_t    toff   = afd >= 0 ? 0 : (off_t)(arena - (uint8_t *)r.base);
    printf(TAG "pid %d uid %d attached: arena %zu KiB, %u slots x %u B\n",
           (int)getpid(), (int)getuid(), alen >> 10, r.mask + 1u, r.slot_size);

    /* ---- step 1: every route to a writable view ---- */
    int writable = 0;
    if (mprotect(arena, alen, PROT_READ | PROT_WRITE) == 0) {
        writable = 1;
        report("mprotect(arena, PROT_WRITE)", 1, 0);
    } else {
        report("mprotect(arena, PROT_WRITE)", 0, errno);
    }

    void *q = mmap(NULL, alen, PROT_READ | PROT_WRITE, MAP_SHARED, target, toff);
    report("mmap(arena fd, PROT_WRITE, MAP_SHARED)", q != MAP_FAILED, errno);
    if (q != MAP_FAILED) munmap(q, alen);

    char path[64];
    snprintf(path, sizeof path, "/proc/self/fd/%d", target);
    int rw = open(path, O_RDWR | O_CLOEXEC);
    if (rw < 0) {
        report("reopen /proc/self/fd O_RDWR, mmap writable", 0, errno);
    } else {
        q = mmap(NULL, alen, PROT_READ | PROT_WRITE, MAP_SHARED, rw, toff);
        report("reopen /proc/self/fd O_RDWR, mmap writable", q != MAP_FAILED, errno);
        if (q != MAP_FAILED) munmap(q, alen);
        close(rw);
    }

    if (prod > 0) try_producer_mem(prod);

    /* ---- step 2: corrupt everything, through whatever this process has ---- */

    /* On a sealed ring the first store below kills this process with SIGSEGV.
     * Left dumpable, the kernel would first hand a core dump to whatever
     * core_pattern names (apport on Ubuntu, systemd-coredump elsewhere), and
     * that can outlast the whole stream: the producer would then find an
     * attacker that has already trapped still alive. Seen on Ubuntu 24.04.
     * Giving up its own core file changes nothing about the attack, and it is
     * done after step 1 so the probes above run exactly as before. */
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    printf(TAG "writing 0xDEADBEEF across the arena%s\n",
           writable ? "..." : " through its read-only mapping...");
    if (ready >= 0) {
        if (write(ready, "r", 1) < 0) { /* producer gone: nothing to tell */ }
        close(ready);
    }

    volatile uint32_t *w = (volatile uint32_t *)arena;
    const size_t nw = alen / sizeof *w;
    for (;;)
        for (size_t i = 0; i < nw; i++) w[i] = 0xDEADBEEFu;
}

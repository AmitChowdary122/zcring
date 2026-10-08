/*
 * attack_demo — Layer 3 Phase 1, shown rather than asserted.
 *
 *   attack_demo --unsealed     today's userspace-only zcring
 *   attack_demo                the same ring, arena sealed (zcring.h §14)
 *
 * One producer publishes a stream of messages over a broadcast ring. One
 * honest consumer checks every byte of every message. One hostile consumer —
 * build/malicious_consumer, a separate executable that attaches through the
 * fds like any other consumer — tries to obtain a writable view of the arena
 * and then writes 0xDEADBEEF across all of it.
 *
 * Same attacker binary, same attack, one flag, and the flag is the
 * producer's. Unsealed, the attack lands and the honest consumer sees
 * corruption. Sealed, the kernel refuses every route, the attacker's first
 * store takes SIGSEGV, and the honest consumer finishes with zero errors.
 *
 * The producer marks itself non-dumpable (PR_SET_DUMPABLE 0). That closes
 * the one route a seal cannot: a same-user process writing straight through
 * the producer's own writable mapping via /proc/<pid>/mem or ptrace. Run as
 * root, the attacker is dropped to uid 65534 first, because root
 * (CAP_SYS_PTRACE) can reach any process's memory and no user-space or
 * per-mapping protection is a defence against it — see §14's threat model.
 *
 * Exit status: 0 if the outcome is the one this mode should produce (sealed:
 * no corruption and the attacker trapped; unsealed: corruption observed),
 * 1 otherwise, 2 on a setup error.
 */
#define _GNU_SOURCE
#include "../src/zcring.h"

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define NOBODY 65534

/* Payload word i of message `seq`. Every word depends on both, so an
 * overwrite, a stale slot and a slot read at the wrong position all show up
 * as a mismatch. */
static inline uint64_t word(uint64_t seq, size_t i)
{
    return (seq + 1) * 0x9E3779B97F4A7C15ULL + i;
}

static int corrupt(const uint64_t *p, size_t nw, uint64_t seq, size_t *at)
{
    for (size_t i = 0; i < nw; i++)
        if (p[i] != word(seq, i)) { *at = i; return 1; }
    return 0;
}

static void honest_consumer(zc_ring_t *r, uint64_t n, int sealed)
{
    /* Forked from the producer, so it inherited the producer's writable
     * mapping. A consumer that is not the producer should not keep one. */
    if (sealed && zc_drop_write(r) != 0) { perror("zc_drop_write"); _exit(2); }
    int id = zc_bcast_join(r);
    if (id < 0) { perror("zc_bcast_join"); _exit(2); }

    const size_t nw = r->slot_size / sizeof(uint64_t);
    uint64_t bad = 0;
    for (uint64_t want = 0; want < n; want++) {
        uint64_t pos; uint32_t len; void *p;
        while (!(p = zc_bcast_acquire(r, id, &pos, &len))) sched_yield();
        size_t at;
        if (corrupt(p, nw, want, &at)) {
            if (bad < 3)
                printf("[consumer]  CORRUPTION: message %lu, word %zu = 0x%016lx\n",
                       (unsigned long)want, at,
                       (unsigned long)((const uint64_t *)p)[at]);
            bad++;
        }
        zc_bcast_release(r, id, pos);
    }
    zc_bcast_leave(r, id);
    printf("[consumer]  %lu messages received, %lu corrupted\n",
           (unsigned long)n, (unsigned long)bad);
    _exit(bad ? 1 : 0);
}

static pid_t launch_attacker(zc_ring_t *r, int sealed, int ready_w)
{
    char self[PATH_MAX], bin[PATH_MAX + 32];
    ssize_t k = readlink("/proc/self/exe", self, sizeof self - 1);
    if (k < 0) return -1;
    self[k] = 0;
    char *slash = strrchr(self, '/');
    if (slash) *slash = 0;
    snprintf(bin, sizeof bin, "%s/malicious_consumer", self);

    pid_t prod = getpid();
    pid_t pid = fork();
    if (pid != 0) return pid;

    /* The fds survive exec only without FD_CLOEXEC, which memfds are created
     * with. dup() gives copies without it; the originals still close. */
    int fd = dup(zc_fd(r));
    int afd = sealed ? dup(zc_arena_fd(r)) : -1;
    if (geteuid() == 0) {
        if (setgroups(0, NULL) != 0 || setgid(NOBODY) != 0 || setuid(NOBODY) != 0)
            perror("[producer]  could not drop the attacker to nobody");
    }
    char a1[32], a2[32], a3[32], a4[32];
    snprintf(a1, sizeof a1, "--fd=%d", fd);
    snprintf(a2, sizeof a2, "--arena-fd=%d", afd);
    snprintf(a3, sizeof a3, "--producer-pid=%d", (int)prod);
    snprintf(a4, sizeof a4, "--ready-fd=%d", ready_w);
    execl(bin, bin, a1, a2, a3, a4, (char *)NULL);
    perror(bin);
    _exit(127);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    int sealed = 1;
    uint64_t n = 200000;
    uint32_t slots = 256, slot_size = 4096;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--unsealed"))          sealed = 0;
        else if (!strncmp(argv[i], "--messages=", 11))    n = strtoull(argv[i] + 11, NULL, 10);
        else {
            fprintf(stderr, "usage: %s [--unsealed] [--messages=N]\n", argv[0]);
            return 2;
        }
    }

    /* Before the ring exists and before anything is forked, so every process
     * that will ever hold the writable mapping is non-dumpable. */
    prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

    zc_ring_t r;
    int rc = sealed ? zc_create_sealed(&r, slots, slot_size, ZC_MODE_BROADCAST)
                    : zc_create_bcast(&r, slots, slot_size);
    if (rc != 0) { perror("[producer]  create"); return 2; }

    printf("\n=== %s ===\n", sealed
           ? "SEALED ring: arena sealed F_SEAL_FUTURE_WRITE (Layer 3 Phase 1)"
           : "UNSEALED ring: userspace-only zcring, every peer maps it read-write");
    printf("[producer]  pid %d  broadcast ring %u x %u B, %lu messages\n",
           (int)getpid(), slots, slot_size, (unsigned long)n);

    pid_t cons = fork();
    if (cons == 0) honest_consumer(&r, n, sealed);
    while (zc_bcast_active(&r) < 1) sched_yield();
    printf("[consumer]  pid %d joined, checking every byte of every message\n",
           (int)cons);

    /* Wait until the attacker has finished probing and is about to write, so
     * that on the unsealed ring its writes overlap the whole stream. EOF
     * instead of a byte means it died before getting that far. */
    int pfd[2];
    if (pipe(pfd) != 0) { perror("pipe"); return 2; }
    pid_t atk = launch_attacker(&r, sealed, pfd[1]);
    close(pfd[1]);
    if (atk < 0) { perror("[producer]  launch attacker"); return 2; }
    char c;
    if (read(pfd[0], &c, 1) < 0) { /* EOF or error: proceed either way */ }
    close(pfd[0]);

    const size_t nw = slot_size / sizeof(uint64_t);
    uint64_t pre_commit_bad = 0;
    for (uint64_t s = 0; s < n; s++) {
        uint64_t pos; uint64_t *p;
        while (!(p = zc_bcast_reserve(&r, &pos))) sched_yield();
        for (size_t i = 0; i < nw; i++) p[i] = word(s, i);
        /* What the producer itself can see: its message damaged between being
         * written and being published. */
        size_t at;
        if (corrupt(p, nw, s, &at)) {
            if (pre_commit_bad < 3)
                printf("[producer]  CORRUPTION: message %lu damaged before commit\n",
                       (unsigned long)s);
            pre_commit_bad++;
        }
        zc_bcast_commit(&r, pos, slot_size);
    }

    int cst = 0, ast = 0;
    waitpid(cons, &cst, 0);
    int stopped = 0;
    if (waitpid(atk, &ast, WNOHANG) == 0) {
        kill(atk, SIGTERM);
        waitpid(atk, &ast, 0);
        stopped = 1;
    }

    if (stopped)
        printf("[producer]  attacker pid %d still writing after the last message; "
               "stopped it with SIGTERM\n", (int)atk);
    else if (WIFSIGNALED(ast))
        printf("[producer]  attacker pid %d killed by %s (%s)\n", (int)atk,
               WTERMSIG(ast) == SIGSEGV ? "SIGSEGV" : "signal",
               strsignal(WTERMSIG(ast)));
    else
        printf("[producer]  attacker pid %d exited %d\n", (int)atk,
               WIFEXITED(ast) ? WEXITSTATUS(ast) : -1);
    printf("[producer]  %lu messages published, %lu damaged before commit\n",
           (unsigned long)n, (unsigned long)pre_commit_bad);

    int consumer_clean = WIFEXITED(cst) && WEXITSTATUS(cst) == 0;
    int trapped = !stopped && WIFSIGNALED(ast) && WTERMSIG(ast) == SIGSEGV;
    int as_expected = sealed ? (consumer_clean && !pre_commit_bad && trapped)
                             : !consumer_clean;
    printf("[result]    %s\n", sealed
           ? (as_expected ? "sealed: the attack trapped in hardware; every message intact"
                          : "UNEXPECTED: sealed ring did not hold — see output above")
           : (as_expected ? "unsealed: one hostile consumer corrupted the stream for everyone"
                          : "UNEXPECTED: attack did not land on the unsealed ring"));
    zc_close(&r);
    return as_expected ? 0 : 1;
}

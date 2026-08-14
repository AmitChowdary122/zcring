---
status: measured
confidence: medium
source: results/tail_1mib.csv (REPS=10, COUNT=5000), results/hugepage_ab.csv
verified: 2026-08-09
---

# Large-payload tail — the bounded worst case survives; the p99.9 claim did not

Two claims came out of the 5-rep run. The 10-rep run **kept one and killed the
other.** This note records both, because the killed one is the more useful
lesson.

## SURVIVED — bounded worst case

At 1 MiB, N=1, per-rep maximum latency:

| arm | max range | reps over 1 ms |
|---|---|---|
| zcring, 4 KiB pages | 684–936 µs | **0 / 10** |
| zcring, huge pages | 674–932 µs | **0 / 10** |
| unix socket | 756–2440 µs | **7 / 10** |

**Twenty zcring runs, none over 1 ms. Ten unix runs, seven over 1 ms, worst
2.44 ms.** This also reproduces the direction of the 5-rep run.

Plausible mechanism — **not yet verified**: a 1 MiB message through a socket
is many syscalls, each a preemption point. zcring's data path has none, so
there are far fewer opportunities to be descheduled mid-message. If that is
right, the bounded tail is a *consequence* of the zero-syscall design rather
than a separate property, which makes it the same claim as
[[claims/memory-passes]] seen from the tail rather than the mean. Attractive
story; confirm before telling it.

## DIED — "zcring's p99.9 is 2.1× better"

The 5-rep run gave zcring 401 µs vs unix 842 µs. **At 10 reps that reverses:**

| | zcring (huge) | unix |
|---|---|---|
| p99.9, mean of reps | 437.8 µs | **327.3 µs** |
| p99.9, median of reps | **253.3 µs** | 315.1 µs |

zcring is *bimodal*: 3 reps in 10 land near 880 µs, the rest near 250 µs.
unix is tightly clustered, 281–375 µs. So zcring's **median** rep is better
and its **mean** rep is worse, and with n=10 neither is a claim.

**Do not quote a p99.9 comparison at 1 MiB.** Quote the bounded max, which is
robust across 20 runs.

## What I got wrong

I read 2.1× off five reps and called it "promising". It was **sampling
noise** — five reps happened to catch two elevated unix reps and few elevated
zcring ones. The caution held (it was marked not-quotable and kept out of the
deck), but the *direction* I reported was wrong, not merely imprecise.

**Lesson: n=5 cannot characterise a p99.9.** p99.9 of 5000 samples is the
fifth-worst observation; its rep-to-rep variance is enormous. For any tail
claim, look at the **per-rep spread before the mean** — the bimodality was
visible in the raw list and invisible in the summary.

This is error-log pattern #6 in [[INDEX]].

## Still true

[[claims/large-payload-n1]] stands: p50 is 0.90–0.92× unix. zcring loses on
median latency at 1 MiB and wins on worst case. **That is the honest framing —
a mean-versus-tail trade-off**, and it is a good one for a problem statement
that asks for determinism. But state both halves.

---
status: refuted
confidence: high
source: results/hugepage_ab.csv
verified: 2026-08-09
---

# REFUTED — TLB pressure does not explain the large-payload loss

The last testable non-platform candidate for [[claims/large-payload-n1]].
**It was not the answer.** But the experiment found something better than what
it was looking for — see [[claims/large-payload-tail]].

## The hypothesis

At 1 MiB with 4 KiB pages each message spans **256 PTEs**, walked once by the
producer and again by the consumer in a different address space, against an
L1 dTLB of ~64 entries. A 2 MiB page makes it **one PTE per side**.

Unlike [[hypotheses/busy-spin-power]] this was arithmetic rather than
speculation, which is why it earned one more session after three negative
results. `src/zcring.h` §11 has the derivation.

## Result — 1 MiB, N=1, 5 reps, `pages=hugetlb` confirmed on every huge row

| metric | 4 KiB | huge | delta |
|---|---|---|---|
| mean | 193.6 µs | 190.3 µs | **+1.7%** |
| p50 | 191.0 µs | 188.1 µs | **+1.5%** |

Ratio vs unix moves **0.939× → 0.955×**. Still a loss.

**1.7% is inside the time-box's "few percent" — this is a negative result.
The mean-latency question is closed.** Remaining candidates are platform
power/frequency properties, not properties of this code. Do not reopen it.

## What the experiment did establish

Huge-page backing is worth keeping anyway: it is free, it never regresses, and
it produces the tail behaviour in [[claims/large-payload-tail]]. The fallback
chain is silent and unconditional so it costs nothing on kernels without
hugetlbfs.

Also banked from the build (commit `67cb601`):

- `--pages=4k` now explicit in `sweep.sh`/`fanout.sh`, so committed datasets
  cannot change meaning depending on whether a pool happened to be reserved.
- `ZC_HUGE_REQUIRE` fails rather than falling back — the [[traps/touch-flag]]
  failure mode designed out in advance. It worked: every huge row in the CSV
  reads `pages=hugetlb`, so no arm silently measured its control.
- A latent leak fixed: `zc_attach()` munmap'd `sizeof(zc_ctrl_t)` after a
  mapping the kernel had rounded to 2 MiB on a hugetlbfs fd — `EINVAL`,
  leaking a pool page per attach.

## Lesson

Four hypotheses tested against this gap, four negative. The discipline that
paid off was **time-boxing in advance** — the stop condition was written down
before the data arrived, so the result was accepted rather than argued with.
That is the thing [[hypotheses/busy-spin-power]] got wrong.

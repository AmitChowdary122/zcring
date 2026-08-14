---
status: measured
confidence: high
source: results/sweep.csv, results/sweep_verify.csv
verified: 2026-08-07
---

# 64 B small-message latency — the headline claim

**18–20× faster than a pipe at 64 B, N=1.**

## Quote it as a range, never as one number

Two independent sessions:

| session | ratio |
|---|---|
| primary (`sweep.csv`) | 19.62× |
| re-measure (`sweep_verify.csv`) | 18.23× |

zcring reproduced within 2% everywhere. The spread is the **comparators** —
pipe and unix came out 5–12% faster at small payloads on the second occasion.
So the honest claim is the range. Quoting 19.6× invites "why doesn't your own
re-run agree?"

That the range exists at all is [[claims/reproducibility]] evidence, not a
weakness. Say it out loud.

## Mandatory qualifiers

- **Spin waiter.** This is a *dedicated-core* number. Under `--notify` it
  collapses to 0.93× — see [[decisions/spin-is-the-sweep-default]]. Never
  show this number without saying which waiter.
- **`--touch`.** [[traps/touch-flag]].
- **Cross-physical-core pinning.** [[traps/smt-siblings]].
- Measured on [[claims/machine]].

## Other sizes

| payload | vs pipe |
|---|---|
| 64 B | 18–20× |
| 1 KiB | ~9.5–10× |
| 4 KiB | 3.66× (conservative); 4.14× on a cleaner re-measure |

**Lead with this claim, not with a bandwidth multiplier.** Small-message
latency is the regime embedded real-time control actually lives in, and it is
where the design wins hardest. The large-payload story is
[[claims/large-payload-n1]] and it goes the other way.

## What would overturn it

A comparator implementation that is materially faster than the harness's pipe
path — e.g. `splice()`/`vmsplice()` rather than `read`/`write`. Not currently
tested. If a judge raises it, concede it's untested rather than defending.

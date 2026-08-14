---
status: decided
confidence: high
verified: 2026-08-01
---

# Never pin a consumer to the producer's SMT sibling

Sibling threads share L1/L2, which **flatters shared-memory IPC dishonestly**.
Cross-physical-core is the realistic case and the honest default.

## What it cost

Measured once on [[claims/machine]]: p50 looked fine while **p99 was inflated
185×**. The damage was invisible in the headline number, which is what makes
it dangerous.

## Now handled

`scripts/sweep.sh` and `scripts/fanout.sh` read `thread_siblings_list` and
pick correctly. **Verify the pinning line they print** — don't assume.

`--cpu-cons` is a *list*; consumer *k* pins to the *k*'th entry, wrapping. In
fan-out it must exclude the producer's sibling.

If you deliberately show a same-core number, **label it**.

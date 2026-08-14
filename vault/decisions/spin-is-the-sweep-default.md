---
status: decided
confidence: high
verified: 2026-08-08
---

# `WAITER=spin` is the sweep default — deliberately

`scripts/sweep.sh` defaults to `WAITER=spin`. Two reasons, and the second is
the important one.

1. Every dataset committed to `results/` was measured that way. Changing the
   default would silently make the committed baseline irreproducible from this
   tree.
2. **Spin and notify answer different questions**, not better and worse
   versions of one:
   - **spin** — "lowest achievable latency given a dedicated core"
   - **notify** — "latency at an idle CPU cost comparable to a socket"

**Report both. Never silently mix them in one table.**

## The number that makes this matter

[[claims/64b-small-message]] is **18–20× under spin and 0.93× under notify.**
Small payloads lose their entire advantage when the consumer blocks, because
the syscall comes back into the data path.

This is *derived, not a bug* — `src/zcring.h` §5 has the analysis. But it
means **the 18–20× headline is a dedicated-core number and must always be
stated as such.** Showing it without the qualifier is the single easiest way
to get caught overstating.

## Two deployment postures

The deck has a slide on exactly this, and it is a better story than picking a
winner: a real-time control loop with a core to spare takes the spin posture;
a general-purpose service that must not burn a core idle takes notify. The
framework supports both, and the adaptive threshold
([[decisions/ai-criterion-answer]]) is what makes notify defensible rather
than a fixed guess.

`--yield` and `YIELD_SPINS` **no longer exist.** Any dataset measured under
them is historical — see [[claims/fanout-crossover]].

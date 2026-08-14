---
status: measured
confidence: high
source: results/fanout_notify.csv
verified: 2026-08-08
---

# Fan-out crossover — at N=2, and not past it

At 1 MiB, under the shipping `--notify` waiter:

| N | vs unix socket |
|---|---|
| 1 | 0.82× (loses — [[claims/large-payload-n1]]) |
| 2 | **1.36× (wins)** |
| 4 | 1.24× — **do not quote as growth** |

**The crossover is real and it is the scalability story.** Publication is
O(1) in N; copying transports are O(N). One `commit`, N readers, zero copies.

## Do NOT claim growth past N=2

The old **2.03× at N=4** was measured under `--yield`, a flag that no longer
exists. Under `--notify`, N=4 gives 1.24×, because `FUTEX_WAKE` makes four
consumers runnable simultaneously on two physical cores and they thunder.
64 B, N=4: 1.03 µs → 12.3 µs.

That is oversubscription, not a transport property.

The `--yield` data is preserved as `results/fanout_yield_historical.csv` —
valid as measured, no longer reproducible from this tree. **It cannot share a
table with `--notify` numbers.**

## Don't fix this with a bigger machine

See [[decisions/no-ryzen-n8-run]]. In broadcast mode every consumer runs on
every message, so consumer count is bounded by core count on *any* platform.
N=2 fits on every embedded part there is.

## Reproducibility

Re-measured from a clean boot into `results/fanout_verify.csv`: the crossover
reproduced within 1.5% at every point (0.82/1.38/2.03× → 0.82/1.38/2.06× at
1 MiB, under the historical `--yield`). Almost no competing submission will
have re-measured itself — say so.

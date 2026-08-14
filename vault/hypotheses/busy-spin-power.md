---
status: refuted
confidence: high
source: results/cstate_waiter_ab.csv, results/cstate_waiter_ab_rate25.csv
verified: 2026-08-08
---

# REFUTED — busy-spin power budget explains the large-payload loss

**This was my hypothesis. It is dead. I pushed it twice after it was already
in doubt. Do not resurrect it.**

## The claim

That [[claims/large-payload-n1]] — the ~65–70% penalty at 1 MiB with C-states
disabled — was caused by zcring's spinning consumer burning power budget,
depressing the package's sustained frequency, and so slowing the copy.

## How it died

- **First test** (`--sleep`): inconclusive. I should have treated
  inconclusive as "stop and reconsider". Instead I argued for the hypothesis
  again.
- **Second test**, a real futex block/wake cycle, C-states off:

| waiter | 1 MiB latency |
|---|---|
| spin | 194.0 µs |
| notify (genuinely blocks) | 197.5 µs |

A consumer that genuinely sleeps shows **the same penalty**. If spinning were
the cause, notify would have recovered it. Conclusive.

## What remains

Offered rate, thermal state, and waiter style are all ruled out. What is left
is platform power/frequency behaviour — i.e. **not a property of this code** —
plus [[hypotheses/tlb-hugepages]], which is the last testable candidate.

## The lesson worth keeping

I proposed a mechanism, got an inconclusive result, and advocated harder
instead of updating. The correct move after test one was to design the
decisive test immediately — which is what test two was, and it took an hour.

This sits in my error log in [[INDEX]] as pattern #3.

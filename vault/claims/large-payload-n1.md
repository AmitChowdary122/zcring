---
status: measured
confidence: high
source: results/sweep.csv
verified: 2026-08-01
---

# Large payloads lose at N=1 — the disclosed weakness

**At 256 KiB–1 MiB, N=1, zcring is 0.68×–0.84× a UNIX socket.** It loses.

This holds under the C-states-disabled configuration the project *requires*
for [[claims/determinism-p999]]. It is a real, disclosed trade-off, not a bug
and not a measurement artifact.

## Ruled out as causes

| candidate | verdict | where |
|---|---|---|
| Offered rate | ruled out | [[hypotheses/offered-rate-confound]] |
| Thermal state | ruled out | thermal gating in `scripts/lib.sh` |
| Busy-spin power budget | **refuted** | [[hypotheses/busy-spin-power]] |
| Waiter style (spin vs real futex block) | ruled out | 194.0 vs 197.5 µs |
| TLB pressure / page size | **pending** | [[hypotheses/tlb-hugepages]] |

Remaining candidates after TLB are platform power/frequency properties —
i.e. not properties of this code. Time-boxed away before the deadline.

## Two claims here died before this one

1. **"~2× floor at 1 MiB"** — measured in a VM. Wrong on bare metal.
2. **"1.1–1.5× large-payload win"** — measured under a C-state configuration
   that predates and conflicts with the tail-latency fix.

**Do not reinstate either.** Both are in my error log in [[INDEX]]. The
pattern: a number that flattered the project survived three documents because
it was copied rather than re-derived.

## How to present it

State it *before* a judge finds it, then pivot to [[claims/fanout-crossover]],
where the large-payload case is genuinely won back. Acknowledging a limitation
first reads as rigor; being caught on it destroys the credibility of
everything adjacent.

The deck's limitations slide and `README.md`'s "Why large payloads lose here"
both carry this.

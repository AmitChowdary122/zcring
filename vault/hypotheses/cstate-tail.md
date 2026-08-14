---
status: confirmed
confidence: high
source: results/cstate_confirm.csv, results/cstate_mechanism.csv
verified: 2026-08-01
---

# Deep C-states are a tail-latency source — CONFIRMED

The single biggest determinism win in the project.

## Idle states on [[claims/machine]]

| state | name | exit latency |
|---|---|---|
| state0 | POLL | 0 µs |
| state1 | C1_ACPI | 1 µs |
| state2 | C2_ACPI | **253 µs** |
| state3 | C3_ACPI | **1048 µs** |

A consumer idle between messages lets the cpuidle governor predict a long
sleep and pick C3. That 1048 µs lands directly in the tail.

## Measured, 64 B, N=1, 5 reps × 50,000 messages

| | p99.9 mean | p99.9 range |
|---|---|---|
| C-states enabled | 354 µs | 787 ns – 1.77 ms |
| `cpupower idle-set -D 0` | **2.4 µs** | 1.4 µs – 4.2 µs |

**The variance collapse matters more than the mean.** Before, p99.9 depended
on whether the governor happened to guess C3 during that run. After, it is
boringly consistent — which is the actual property a determinism claim needs.

## Superseded sub-claim — do not repeat

*"pipe/unix are not affected by the C-state setting"* was **rate-specific and
is now known to be false at low message rates.** At a 2 ms gap, unix is
**36.7% faster** with C-states disabled, because a blocking consumer's core
pays idle-state exit latency on wake.

Do not repeat the old general form. This is a good example of a claim that was
true where it was measured and false where it was then applied.

## Cost

Disabling C-states has a **thermal cost** on this two-physical-core part —
see `RUNNING.md` §4b. It is a benchmarking configuration, not a deployment
recommendation. Restore with `sudo cpupower idle-set -E`.

It is also a *precondition* for [[claims/determinism-p999]], not a complete
fix — p99.99 remains open.

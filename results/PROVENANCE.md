# Provenance of the committed datasets

## Machines

| Machine | Role |
|---|---|
| **Intel Core i3-1115G4** — 2 physical cores + SMT, bare metal | **Canonical.** Died 14 Aug 2026 (drive fault); **revived 20 Aug**. |
| **AMD Ryzen 9 270** — 8C/16T | Secondary, labelled `_ryzen`. |

All canonical measurements are on the i3: `--touch`, C-states disabled,
performance governor, producer and consumer pinned to distinct physical cores,
5 repetitions. Methodology in `README.md`.

## Two code revisions, and which datasets belong to which

**This is the thing to get right before quoting any number.**

| Dataset | Machine | Code | Status |
|---|---|---|---|
| `sweep.csv` | i3 (revived) | **HEAD, 20 Aug** | **canonical** |
| `sweep_i3rev.csv` | i3 (revived) | HEAD, 20 Aug | independent re-run; agrees to 3 s.f. |
| `sweep_layer1_historical.csv` | i3 | `847bee2`, 1 Aug | historical — not reproducible from this tree |
| `sweep_verify_layer1_historical.csv` | i3 | pre-Layer-2, 7 Aug | historical |
| `sweep_ryzen.csv` | Ryzen | HEAD, 20 Aug | secondary, labelled |
| `fanout_notify.csv` | i3 | 8 Aug | **pre-dates huge pages + portability audit** |
| `fanout_yield_historical.csv` | i3 | measured under `--yield`, removed | historical |
| `cstate_*.csv`, `hugepage_ab.csv`, `tail_1mib.csv`, `sweep_rate{10,50}.csv` | i3 | various | supporting studies |
| `ryzen_bringup.txt` | Ryzen | 17 Aug | bring-up log |

**A code regression sits between the two revisions.** `zc_ctrl_t` grew across
adaptive notification, huge-page backing and the portability audit, which
moves `slots_off`/`arena_off` and costs ~16 ns per small message. Established
by a same-session A/B alternating binaries on one machine: `847bee2` returned
**114 ns** at 64 B — the historical number exactly — while HEAD returned
**130 ns**. So the machine is sound and the drift is ours. Headline is
**16.8×**, not the historical 19.6×.

**Never mix revisions in one table.** The same rule that applies to mixing
machines applies to mixing code.

## The Ryzen dataset — what it is and is not for (20 Aug)

`sweep_ryzen.csv` is the identical sweep (`--touch`, 5 reps, performance
governor, deep C-states disabled, producer and consumer pinned to distinct
physical cores) on the replacement machine. It exists to answer one question:
**do the conclusions survive a different microarchitecture?**

**Quotable from it: p50.** It reproduces to three significant figures across
all five repetitions at every payload size.

**NOT quotable from it: any tail statistic.** p99.99 and max are *bimodal*
across repetitions — e.g. at 4 KiB the five p99.99 values are 8.6, 1217.8,
1075.2, 1215.1 and 0.7 µs, with p50 constant at 0.4 µs throughout. A p50 that
stable alongside a tail that unstable is external interference on a machine
that was not fully quieted, not a property of the transport. Re-run on a
quiesced machine before quoting any Ryzen tail number.

**The headline claims stay anchored to the i3** regardless of the Ryzen being
faster. 2–4 cores is representative of embedded deployment; an 8C/16T desktop
part is not, and "our numbers got bigger on a bigger CPU" is not a result.

## What the Ryzen dataset established

The i3's large-payload loss (0.68× at 256 KiB, 0.84× at 1 MiB) **inverts on
Zen** — 3.62× and 3.08×. zcring's 1 MiB p50 falls 183 µs → 44 µs while the
UNIX socket barely moves, 155 µs → 136 µs.

This is the fifth test of that gap. Offered rate, thermal state, busy-spin
power draw and TLB pressure were each tested and each came back negative,
leaving platform power/frequency behaviour as the only remaining candidate. A
different platform was the most direct test available, and the prediction
held.

**Evidence, not proof — but better evidence than it was.** Both sweeps are now
the **same binary** (HEAD), measured within hours of each other on 20 Aug, so
the code variable is controlled. What remains uncontrolled is everything else
about the two parts: per-core memory bandwidth, fabric clock and prefetch all
differ independently of anything tested, and 183 µs at 1 MiB is ≈5.7 GB/s — a
memory-bandwidth-shaped number. So claim that **the cost tracks platform
memory and frequency behaviour rather than the transport design**; do not
claim an isolated mechanism.

Now that the i3 is revived, a genuinely controlled experiment is possible —
same binary, same day, both machines, with the specific platform variables
(uncore frequency, RAPL budget, per-core bandwidth) instrumented rather than
inferred. That is Stage 2 work, not Stage 1 work.

Nothing else in this directory was measured on the Ryzen. If that changes, add
the rows above in the same commit as the data.

# Provenance of the committed datasets

**Every CSV in this directory was measured on an Intel Core i3-1115G4** —
2 physical cores plus SMT, bare-metal Ubuntu, C-states disabled, producer and
consumer pinned to distinct physical cores. Methodology in `README.md`.

## The measurement machine no longer exists

On **14 Aug 2026** that laptop failed. Its SSD was moved into an **AMD Ryzen 9
270 (8C/16T)** and now runs there as a dual boot.

**Consequences, stated plainly because a judge may ask:**

- The committed numbers remain valid *as measured*. Nothing about them
  changed; the methodology, the raw data and the analysis scripts are all in
  this repo.
- They are **no longer reproducible by us on demand**, because we no longer
  own the hardware they were taken on. They remain reproducible by anyone with
  an equivalent part, which is the sense in which the reproducibility claim in
  `README.md` was ever meant.
- The claim that the full suite was **re-measured on a separate occasion and
  reproduced within 1.5%** (`sweep_verify.csv`, `fanout_verify.csv`) is
  unaffected. Both runs happened on the i3, weeks apart, from a clean boot.

## Why the numbers are not being re-taken on the Ryzen

The headline claims are deliberately anchored to hardware representative of
**embedded deployment**, where 2–4 cores is typical. An 8-core desktop part is
*less* representative of the problem statement, not more — the same reasoning
that led to dropping a planned Ryzen scaling run before the laptop failed.
A dead laptop does not change which platform the claims should be about.

## If the Ryzen is measured

It would be a **secondary, clearly-labelled dataset**, and the interesting
question is not "are the numbers better" but **"do the conclusions survive a
completely different microarchitecture?"** — Zen vs Tiger Lake, 8 cores vs 2,
different cache hierarchy and idle-state behaviour. Structural agreement
across two unlike parts is stronger evidence than a third run on the same box
would have been.

Rules for any such run:

- **`OUT_SUFFIX` is mandatory** (`_ryzen`). `scripts/lib.sh:check_machine()`
  enforces this and will refuse to write a canonical filename on a CPU that
  isn't the i3.
- Record the CPU, kernel and date in the table below.
- Never merge two machines into one table, chart or claim.

## Dataset index

| Files | CPU | Notes |
|---|---|---|
| `sweep.csv`, `sweep_verify.csv`, `sweep_rate{10,50}.csv`, `sweep_notify_reps1.csv` | i3-1115G4 | baseline payload sweeps |
| `fanout_notify.csv`, `fanout_yield_historical.csv`, `fanout_verify.csv`, `fanout_rate{10,50}.csv`, `fanout_1mib_unsaturated.csv` | i3-1115G4 | `_yield_historical` measured under a flag that no longer exists |
| `cstate_*.csv` | i3-1115G4 | idle-state tail-latency study |
| `hugepage_ab.csv`, `tail_1mib.csv` | i3-1115G4 | huge-page A/B and 1 MiB tail characterisation |

| `sweep_ryzen.csv`, `sweep_ryzen.log`, `ryzen_bringup.txt` | **AMD Ryzen 9 270 (8C/16T)** | **secondary, labelled** — see below |

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

This is the fifth and decisive test of that gap. Offered rate, thermal state,
busy-spin power draw and TLB pressure were each tested and each came back
negative, leaving platform power/frequency behaviour as the only remaining
candidate. A different platform was the direct test of that prediction, and it
confirmed it. The disclosed weakness is a property of one CPU, not of the
design.

Nothing else in this directory was measured on the Ryzen. If that changes, add
the rows above in the same commit as the data.

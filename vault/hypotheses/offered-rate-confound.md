---
status: confirmed
confidence: high
source: scripts/rates.sh, results/sweep_rate10.csv, results/sweep_rate50.csv
verified: 2026-08-01
---

# CONFIRMED — a flat `--gap-us` was confounding the sweep

## The problem

A single flat inter-message gap means very different things at 64 B and at
1 MiB. At large payloads a fixed gap saturates the transport, so the benchmark
measures **queueing delay** rather than transport latency — and queueing delay
depends on throughput, not on the property being claimed.

Symptom: three inconsistent 1 MiB ratios across sessions that all looked like
noise and weren't.

## The fix

`RATE_FRACTION` — offered rate is set per size as a percentage of **that
size's measured saturation rate**. Default 25, i.e. a quarter of saturating
throughput, giving 4× headroom above the confirmed knee. `scripts/rates.sh`
holds the table.

Sensitivity checked at 10% and 50% (`sweep_rate10.csv`, `sweep_rate50.csv`) —
the conclusions hold across all three. **Publishing the sensitivity check is
worth more than the primary number**, because it is what distinguishes a
measurement from a lucky configuration.

## Generalisable lesson

A benchmark parameter that is constant across conditions is not thereby
*fair* across conditions. Ask what the parameter means at each end of the
sweep.

This is the mechanism behind one of the retracted claims in
[[claims/large-payload-n1]].

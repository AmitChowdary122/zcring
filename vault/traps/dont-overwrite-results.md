---
status: decided
confidence: high
verified: 2026-08-08
---

# Never re-run a sweep script without `OUT_SUFFIX`

**I destroyed committed data doing this.**

I handed over `REPS=5 ./scripts/fanout.sh` with no `OUT_SUFFIX`. It overwrote
`results/fanout.csv` — the historical `--yield` dataset that
[[claims/fanout-crossover]] rested on.

Recovered with `git show 847bee2:results/fanout.csv`, then split into
`fanout_yield_historical.csv` and `fanout_notify.csv`. **Recoverable only
because it had been committed.**

## Rules

- Any re-run of `sweep.sh` / `fanout.sh` gets an explicit `OUT_SUFFIX`.
- Commit raw CSVs *before* re-running anything.
- When a flag changes meaning (`--yield` → `--notify`), rename the old dataset
  to say which flag produced it rather than leaving a generic name that will
  be assumed current.

## Why the generic name was the real bug

`fanout.csv` didn't say what produced it, so it was overwritable without
anything looking wrong. Datasets whose names encode their configuration can't
be silently replaced by a different configuration.

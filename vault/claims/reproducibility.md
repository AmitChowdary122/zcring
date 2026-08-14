---
status: measured
confidence: high
source: results/sweep_verify.csv, results/fanout_verify.csv
verified: 2026-08-07
---

# The whole dataset was re-measured — say this out loud

The full suite was regenerated on a **separate occasion, from a clean boot,
with the machine re-quieted from scratch**, into `results/sweep_verify.csv`
and `results/fanout_verify.csv`.

- Fan-out crossover reproduced within **1.5%** at every point
  (0.82/1.38/2.03× → 0.82/1.38/2.06× at 1 MiB).
- zcring itself reproduced within **2%** everywhere.
- The comparators moved 5–12% at small payloads — which is why
  [[claims/64b-small-message]] is quoted as a range.

## Why this scores

In rubric terms this is **Technical Feasibility**. Reproducibility counts as
feasibility evidence, and pinned environments, scripted benchmarks and
committed raw data are worth real points.

**Almost no competing submission will have re-measured itself.** Most will
report one run. Saying "we ran it twice, weeks apart, and here is where the
two disagree" is cheap for us and very hard for them to match.

That the two runs *disagree slightly* is the strongest part. A dataset that
reproduces to four significant figures looks fabricated; one that reproduces
within 2% with a stated discrepancy on the comparators looks measured.

Supporting discipline: thermal gating (`wait_for_cool`), justified per-size
offered rate ([[hypotheses/offered-rate-confound]]), and `REPS=5` minimum.
`README.md` has the full methodology.

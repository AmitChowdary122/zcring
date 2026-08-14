---
status: measured
confidence: high
source: results/cstate_confirm.csv
verified: 2026-08-01
---

# Determinism — p99.9 is quotable, p99.99 is not

**Quotable:** p99.9 at N=1 with deep C-states disabled — **1.04 µs mean,
sub-2 µs range.**

**Not quotable:** p99.99 and max still show occasional excursions into the
hundreds of microseconds (mean ~200 µs / ~360 µs in the same run). A smaller,
separate noise source — most likely IRQ or scheduling jitter.

**Say the boundary out loud.** "p99.9 is 1.04 µs; p99.99 is not yet clean and
here is the work that would close it" is a stronger position than a quiet
p99.9-only table that invites the obvious next question.

## Why this matters most

The problem statement asks for *deterministic* communication. **Most competing
teams will report means.** Reporting a tail distribution at all is a
differentiator; reporting where it stops being good is a bigger one.

## The precondition

Deep C-states must be disabled — [[hypotheses/cstate-tail]]. Without it p99.9
is 354 µs with a range spanning three orders of magnitude.

## Work that would close p99.99

`isolcpus`, `nohz_full`, PREEMPT_RT. Not yet done. C-state disabling is a
*precondition* for that work to be measurable, not a substitute for it.

Also outstanding: jitter under `stress-ng` load. A determinism claim measured
only on a quiet machine is weaker than one that survives contention.

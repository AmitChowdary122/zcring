---
status: decided
confidence: high
source: src/zcring.h, docs/architecture.py
verified: 2026-08-01
---

> Structural fact about the design, verifiable from the code — **not** a
> measurement. The measured ratios live in [[claims/64b-small-message]] and
> [[claims/large-payload-n1]], and one of them contradicts the naive
> prediction. See below.

# Two memory passes instead of four

A pipe or socket write costs **four** passes over the payload: user→kernel
copy, kernel→user copy, and the reads on each side. zcring costs **two** —
the producer writes once, the consumer reads once, in place.

It also **removes the syscall from the data path entirely**. That is the
cleaner claim and the one that survives scrutiny best.

## The trap: 2-vs-4 does not mean 2×

Halving memory passes does **not** yield 2× at large payloads. It doesn't even
yield 1× — see [[claims/large-payload-n1]], where we lose. The kernel's
`copy_to_user` path is heavily optimised (`rep movsb` / ERMS), and at sizes
that exceed cache the coherence traffic of moving dirty lines between cores
costs more than the arithmetic suggests.

**Never present 2-vs-4 as if it predicts the benchmark.** Present it as the
mechanism, then show measurements that partly contradict the naive prediction.
Judges reward the second framing and punish the first.

Where the pass reduction *does* pay off cleanly is
[[claims/64b-small-message]] — small payloads, where syscall overhead
dominates the copy — and [[claims/fanout-crossover]], where N consumers means
N copies avoided.

The architecture diagram carries the 2-vs-4 table in its right rail.

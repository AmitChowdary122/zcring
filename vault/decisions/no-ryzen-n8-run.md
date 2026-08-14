---
status: decided
confidence: high
verified: 2026-08-09
---

# Dropped: the Ryzen N=8 scaling run

**Do not reinstate this. The user was right and I was wrong.**

I had recommended a live-USB run on the 8-core Ryzen to measure fan-out past
N=2, on the theory that [[claims/fanout-crossover]] was limited by having only
two physical cores. The user asked: *"why do we have to do it in the ryzen
system, this is for embedded linux systems, aren't they like 2-4 cores max?"*

That is correct, and it dissolves the premise.

## The argument

In broadcast mode **every consumer runs on every message.** So the practical
consumer count is bounded by available cores on *any* platform. One producer
plus four consumers oversubscribes a four-core embedded target exactly as it
oversubscribes this laptop. The N=4 thundering-herd result is therefore a
**deployment property, not a measurement defect**.

A prettier N=8 number off a desktop CPU would be **less** representative of an
embedded problem statement, not more.

The crossover sits at **N=2**, which fits on every embedded part there is, and
[[claims/machine]] measures that fine.

## How this is now framed

Slide 11 and `README.md` both say *"consumer count is bounded by core count,
on any platform"* — an engineering fact — rather than the earlier *"unmeasured
on adequate hardware"*, which was an apology for owning a small laptop.

The reframe is strictly stronger. Same limitation, and now it reads as
understanding the deployment rather than lacking equipment.

## The general lesson

I reached for more hardware when the right move was to question whether the
number was worth having. Worth remembering next time a limitation looks like
it needs a bigger machine.

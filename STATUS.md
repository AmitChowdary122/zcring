# STATUS — session handoff

Last updated: 1 Aug 2026. Read this after `CLAUDE.md` and before doing
anything. It records decisions and open problems that exist nowhere in the
code, and that a fresh session will otherwise get wrong.

## Where the project stands

- **Layer 1 complete** — lock-free MPMC ring over memfd. TSan clean.
- **Layer 2 fan-out complete** — broadcast to N consumers, per-consumer
  cursors, min-cursor gating, backpressure (not drop), join/departure/reap
  handshake. ABI v2. TSan clean. Design rationale is in the header block of
  `src/zcring.h` — read it before changing anything there.
- **Not started**: adaptive spin-then-futex notification, crash recovery for
  producers, determinism rigor (isolcpus / nohz_full / PREEMPT_RT), iceoryx
  and ZeroMQ comparators, demo, presentation.

## Open problem #1 — the offered-rate inconsistency (BLOCKS THE ABSTRACT)

The same nominal configuration (1 MiB, one consumer) currently has three
different published ratios in this repo:

| source | gap | zcring | unix | ratio |
|---|---|---|---|---|
| Layer 1 baseline | 100 µs | 123.3 µs | 181.1 µs | 1.47× |
| Fan-out sweep | 100 µs | 117.9 µs | 163.0 µs | 1.38× |
| Fan-out unsaturated | 2000 µs | 107.8 µs | 250.0 µs | **2.32×** |

The third is the most flattering and the least safe. `unix` got *slower* when
offered load was *reduced*, which is backwards until you account for cache
temperature: at 2000 µs spacing the buffers go cold between messages, so
copy-based transports pay full cache-miss cost on every copy while zcring's
single pass is less exposed.

**The effect is real, but it means the headline large-payload ratio is a
function of message rate.** Quoting 2.32× invites a judge to rerun at a
different gap, get 1.38×, and conclude the rate was cherry-picked.

**Required fix before any number goes in the abstract:** choose one offered
rate as an explicit methodology decision (stated fraction of measured
saturation, same rule at every payload), rerun *both* the baseline and
fan-out sweeps there, and add a sensitivity check at two other rates showing
the qualitative conclusions hold. Then delete the superseded tables rather
than leaving several versions in the repo.

## Open problem #2 — deep C-states pollute the tails

Hypothesised, not yet confirmed at time of writing. This machine exposes
`C3_ACPI` with a **1048 µs exit latency**, which matches an observed 1220 µs
p99.9 at 64 B / N=1 almost exactly. It also explains an otherwise impossible
result — tail latency *improving* as consumers are added (more consumers keep
cores busy, so they never reach C3).

Confirm with `sudo cpupower idle-set -D 0` and rerun. If confirmed, deep
C-state disabling belongs in the quieting checklist alongside the governor,
and the exit-latency table is good material for the determinism section: a
concrete, measurable, hardware-level jitter source with a documented fix.

**p99.9 is not quotable until this and the isolcpus/RT work are done.**

## Open problem #3 — fan-out scaling is capped by the measurement hardware

The measurement box has 2 physical cores, and one hyperthread is already
sacrificed to keep the producer's core clean. N=4 oversubscribes regardless
of implementation quality. The N=1 → N=2 → N=4 *trend* is real and
defensible; the N=4 *absolute* numbers are a property of the box.

**Unexplored option:** boot the Ryzen laptop (8 physical cores) from an
Ubuntu **live USB**. That gives bare metal without repartitioning, and turns
"trend only, N≤2" into a real N=1,2,4,8 scaling curve — on the one rubric
criterion currently limited by hardware rather than design. Worth an
afternoon if the abstract will claim scaling beyond N=2.

Note: WSL2 on that machine remains disqualified for measurement. Live USB is
a different thing and is valid.

## Traps already hit once — do not repeat

- **`--touch` is mandatory.** Without it the harness moves 8 bytes and
  asserts the rest, producing a fictitious flat curve. Cost us one wasted
  sweep.
- **Never pin a consumer to the producer's SMT sibling.** Did this once;
  p50 looked fine while p99 was inflated 185×.
- **Spinning consumers oversubscribe.** At N=4 on 4 hardware threads the
  benchmark measured scheduler thrash, a 40× artifact. `--yield` is the
  current stopgap; adaptive futex notification is the real fix.
- **`git add -A` in the Windows working tree is unsafe.** That tree only ever
  has current doc edits; everything else in it is stale. Once silently
  reverted a Makefile fix and clobbered a bare-metal dataset. Stage files by
  name there.
- **Benchmarks at a saturating offered rate are not like-for-like.** A 64 MiB
  ring converts overload into queueing delay; a 200 KB socket buffer converts
  it into backpressure. Establish the saturation point first.

## Next steps, in order

1. Confirm the C-state hypothesis; fold into the quieting checklist. *Sonnet.*
2. Regenerate one coherent dataset at a single justified offered rate,
   baseline and fan-out, and retire the superseded tables. *Sonnet.*
3. Optional but recommended: live-USB scaling run on the Ryzen for N=8.
   *Sonnet.*
4. **Draft the abstract.** Target submission 8–10 Aug; window closes 25 Aug.
   *Opus.*
5. Adaptive spin-then-futex notification — removes the `--yield` stopgap and
   makes idle CPU cost comparable to the blocking comparators. *Opus, and
   only after the abstract.*

## Model policy

Sonnet 5 by default. Escalate to Opus 5 for two things only: anything
modifying `src/zcring.h`, and strategic/writing work (abstract, slides,
deciding what to claim). Switch at session boundaries, not mid-session — a
mid-session switch invalidates the prompt cache and re-reads the whole
context.

## The story the abstract should tell

Syscall-free small-message latency (18.8× at 64 B, 7.8× at 1 KiB) for
embedded real-time control traffic, with an advantage that *multiplies with
consumer count* (256 KiB: 1.09× at N=1 → 2.47× at N=4) because publication is
one release store regardless of N while copy-based transports pay N copies
each way.

State the large-payload single-consumer convergence (1.1–1.5×) yourself
before a judge finds it. Do not lead with a bandwidth multiplier.

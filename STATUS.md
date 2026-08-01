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

**Status: half done.** The *mechanism* exists — `scripts/rates.sh` holds a
per-size saturation table and `sweep.sh` now takes `RATE_FRACTION` (default
25, i.e. a quarter of each size's measured saturation rate, 4× headroom above
the knee). **The datasets have not been regenerated with it.**

- `results/sweep.csv` is still the old flat-gap baseline (timestamp 02:46,
  predates the rate work). **Do not quote from it.**
- `results/fanout.csv` was run at a flat `GAP=100`, so it carries the same
  problem.

**Remaining work:** rerun both sweeps at `RATE_FRACTION=25`, REPS=5, C-states
disabled; add a sensitivity check at two other fractions showing conclusions
hold; then delete superseded tables from README.md rather than leaving
several versions in the repo. Until that is done, no large-payload ratio is
safe to publish.

## Open problem #2 — deep C-states — RESOLVED 1 Aug 2026

Confirmed. `C3_ACPI` on this machine has a **1048 µs exit latency**, and it
was landing directly in the tail. Measured at 64 B / N=1, 5 reps:

| | p99.9 mean | p99.9 range |
|---|---|---|
| C-states enabled | 354 µs | 787 ns – 1.77 ms |
| `cpupower idle-set -D 0` | **2.4 µs** | 1.4 µs – 4.2 µs |

The headline is not the mean dropping — it is the **variance collapsing**.
Before, p99.9 depended on whether the cpuidle governor happened to pick C3
during that run; after, it is boringly consistent. Consistency is the
property a determinism claim actually needs.

Full detail and the re-enable command are in `RUNNING.md` §4a. `sweep.sh` and
`fanout.sh` now warn if any state above C1 is enabled.

**This does not fully close the tail.** p99.99 and max still show excursions
into the hundreds of microseconds — a separate, smaller noise source, most
likely IRQ or scheduling jitter. That is what the `isolcpus` / `nohz_full` /
PREEMPT_RT work is for; C-state disabling is a precondition for that work to
be measurable, not a substitute.

**p99.9 is now quotable at N=1 with C-states disabled.** p99.99 is not.

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
- **`cpupower idle-set -D 0` is not sticky across reboots, and a "fixed"
  machine can still show a polluted tail.** Confirmed this again 1 Aug 2026
  (2nd session, same day): after a reboot C-states were back to default
  (`powersave`, C3 enabled). Re-ran the before/after pair — before matched
  the known signature (p99.9 mean 579.9 µs, range 188.2 µs – 1.94 ms over 5
  reps). The *first* after-attempt still showed p99.9 in the tens-to-hundreds
  of µs, which looked like the fix hadn't worked — the actual cause was
  Firefox and other GUI processes competing for the pinned producer core
  (`--cpu-prod=2`). Killing Firefox and re-running gave the expected result
  (p99.9 mean 1.04 µs, range 0.96–1.18 µs). **Always check `ps -eo psr,comm`
  against the pinned CPUs before trusting an "after" number that doesn't
  match expectations — don't conclude the hypothesis is wrong before ruling
  out machine noise.** Full table now also in README.md's determinism
  section.

## Next steps, in order

1. **Regenerate both datasets at `RATE_FRACTION=25`**, REPS=5, C-states
   disabled — baseline and fan-out. Add a sensitivity check at two other
   fractions. Update README.md tables and delete the superseded ones.
   *Sonnet.* This is the only thing blocking the abstract.
2. Optional but recommended: live-USB scaling run on the Ryzen for N=8.
   *Sonnet.* Do this only if the abstract will claim scaling beyond N=2.
3. **Draft the abstract.** Target submission 8–10 Aug; window closes 25 Aug.
   *Opus.*
4. Adaptive spin-then-futex notification — removes the `--yield` stopgap and
   makes idle CPU cost comparable to the blocking comparators. *Opus, and
   only after the abstract.*
5. Producer crash recovery; determinism rigor (isolcpus / nohz_full /
   PREEMPT_RT); iceoryx and ZeroMQ comparators; demo; presentation.

`reports.txt` holds the full Layer 1 and Layer 2 session reports, including
the fan-out design rationale and the bufferbloat analysis in §12. Note that
its §15 predates the C-state confirmation above and says "not yet confirmed"
— that is stale; `RUNNING.md` §4a is authoritative.

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

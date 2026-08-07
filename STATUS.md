# STATUS — session handoff

Last updated: 8 Aug 2026. Read this after `CLAUDE.md` and before doing
anything. It records decisions and open problems that exist nowhere in the
code, and that a fresh session will otherwise get wrong.

## Where the project stands

- **Layer 1 complete** — lock-free MPMC ring over memfd. TSan clean.
- **Layer 2 fan-out complete** — broadcast to N consumers, per-consumer
  cursors, min-cursor gating, backpressure (not drop), join/departure/reap
  handshake. ABI v2. TSan clean. Design rationale is in the header block of
  `src/zcring.h` — read it before changing anything there.
- **Layer 2 adaptive notification complete** (8 Aug) — spin-then-futex with
  the spin budget learned online from the observed inter-arrival
  distribution. `--yield` and `YIELD_SPINS` are **removed**; `--notify` is
  the replacement. **ABI still v2** — nothing in `zc_ctrl_t` moved, and
  `_Static_assert`s in the header now enforce that. TSan clean. Full
  derivation in `src/zcring.h` §§5–10. See "Adaptive notification" below for
  what a fresh session needs to know.
- **Not started**: `eventfd`/`epoll` bridge, crash recovery for producers,
  determinism rigor (isolcpus / nohz_full / PREEMPT_RT), iceoryx and ZeroMQ
  comparators, presentation.

## Read this before quoting any fan-out number

`results/fanout.csv` and `results/fanout_verify.csv` were measured with
`--yield`, which no longer exists. They are still valid *as measured* and are
still what the fan-out crossover claim (0.82× → 1.38× → 2.03× at 1 MiB) rests
on, but they **cannot be regenerated from this tree** and **must not appear in
the same table as a notify-mode number**. `scripts/fanout.sh` now runs
`--notify`. Regenerating that sweep is the highest-value measurement
outstanding — it is also the first real test of whether adaptive notification
removes the 64 B / N=4 spinner-oversubscription artifact (1.03 µs vs ~120 ns
at N=1/2), which it should.

`results/sweep.csv` is unaffected: `scripts/sweep.sh` still defaults to
`WAITER=spin`, deliberately, so the committed baseline stays reproducible.
`WAITER=notify` produces the blocking-consumer variant.

## Open problem #1 — the offered-rate inconsistency — RESOLVED, same-day follow-up session

The same nominal configuration (1 MiB, one consumer) currently has three
different published ratios in this repo:

| source | gap | zcring | unix | ratio |
|---|---|---|---|---|
| Layer 1 baseline | 100 µs | 123.3 µs | 181.1 µs | 1.47× |
| Fan-out sweep | 100 µs | 117.9 µs | 163.0 µs | 1.38× |
| Fan-out unsaturated | 2000 µs | 107.8 µs | 250.0 µs | **2.32×** |

The third is the most flattering and the least safe. `unix` got *slower* when
offered load was *reduced*, which was originally attributed to cache
temperature (copy-based transports pay full cache-miss cost on every copy
once buffers go cold between messages). **That explanation is now believed
incomplete — see the update below.** Thermal throttling was an uncontrolled
variable across all three runs and is likely a bigger contributor than cache
temperature was.

**The effect is real, but it means the headline large-payload ratio is a
function of message rate.** Quoting 2.32× invites a judge to rerun at a
different gap, get 1.38×, and conclude the rate was cherry-picked.

**Update, same-day follow-up session:** attempted the regen and found the
reversal wasn't about offered rate at all — first thermal (Open problem
#4), then, once thermal was controlled for, a THIRD cause: disabling deep
C-states costs zcring ~65% at 1 MiB, independent of both rate and
temperature (Open problem #5). That is now the accepted, disclosed
explanation for why zcring loses at 256 KiB–1 MiB in the current dataset —
see README.md's "Why large payloads lose here" section.

**Status: RESOLVED.** `results/sweep.csv` and `results/fanout.csv`
regenerated at `RATE_FRACTION=25`, REPS=5, C-states disabled, thermal gate
active (`wait_for_cool()` per invocation — see Open problem #4). Sensitivity
check at `RATE_FRACTION=10` and `50` (reduced scope: REPS=3, sizes 64 B/4
KiB/1 MiB) in `results/{sweep,fanout}_rate{10,50}.csv`. README.md's results
tables, offered-rate methodology section, and fan-out results section are
updated from this dataset; superseded numbers (the old 1.47×/1.38×/2.32×
figures and the "~2× floor" / "1.1× floor" corrections) are removed rather
than left alongside the new ones.

The *first* `RATE_FRACTION=25` regen attempt (before the thermal gate
existed) is not in git history in any meaningful way — it was overwritten
by the gated regen before commit. reports.txt §19 has its numbers for the
record.

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

## Open problem #4 — thermal throttling under sustained large-payload load — RESOLVED, same-day follow-up session

Found while chasing Open problem #1: regenerating the offered-rate dataset
produced a result that looked broken — zcring *losing* to unix at 1 MiB
(210 µs vs 162 µs), flatly contradicting the established 1.1–1.5× win. A
controlled gap/`--yield` test ruled out offered rate as the cause (all four
combinations landed within ~3% of each other).

Root cause: this chip has 2 physical cores; cpu1/cpu3 and cpu0/cpu2 are the
two SMT pairs. P-state is shared per physical core, not per logical CPU, so
disabling C-states on the pinned benchmark CPUs (2, 3 — required by Open
problem #2's fix) keeps **both** physical cores continuously active for the
whole session, with no idle state available on either sibling. This
low-TDP embedded part heat-soaks into thermal throttling (confirmed:
`x86_pkg_temp`/`TCPU` at 97–100 °C, nonzero throttle counters) within tens
of minutes of continuous benchmarking.

Confirmed directly: package cool (54 °C) vs throttling (97–100 °C),
identical config (1 MiB, N=1, gap=100 µs) — zcring p50 122.9 µs vs
210–217 µs. Matches the historical 123 µs claim exactly when cool. Degrades
*progressively* across a sweep because sizes run ascending (small → large),
so heat accumulates right into the sizes the large-payload story depends on
— and zcring degrades far more than pipe/unix (~70% vs ~0–20%) because its
consumer busy-spins the gap (generating its own heat) where pipe/unix block.

**This likely explains part of Open problem #1's original three-way
inconsistency too** — thermal state was uncontrolled in all three of those
runs, not just offered rate.

**Fix, landed, revised twice:** `scripts/lib.sh`'s `wait_for_cool()` gates on
the package thermal zone (`x86_pkg_temp`, falls back to `TCPU`) before
**every individual bench invocation** in both `sweep.sh` and `fanout.sh` —
gating once per payload size wasn't fine-grained enough, since the package
reheats from a cool start back to throttling within a single size's 15-run
block. Second revision: found the package **plateaus at 74–81 °C with
C-states disabled and does not converge further** (confirmed: oscillated in
that band over 60s of idle wait, no bench running) — because there's no idle
state left for the pinned cores to actually cool into. So the gate now
temporarily re-enables C-states system-wide during the wait (letting the
package actually reach the low-60s/50s), then re-disables before returning
control to the caller. Costs ~2 min per real cooldown, but it's the only way
the gate converges at all. Documented as `RUNNING.md` §4b. Full
investigation in `reports.txt` §19.

**Not just a benchmarking nuisance — a real finding.** A sustained-throughput
embedded deployment on similarly TDP-constrained hardware would hit the same
wall. Worth a line in the abstract/slides: it's independent supporting
evidence for "small messages, not bulk transfer" as the design's sweet spot,
and for why Layer 2 notification (not just fan-out) matters — a blocking
consumer doesn't self-heat the package the way a spinning one does.

## Open problem #5 — deep C-states cost zcring ~65% at large payloads, independent of heat — RESOLVED (disclosed, not fixed), same-day follow-up session

Found while re-verifying Open problem #4's fix: even with the thermal gate
working correctly (package cool before every invocation), the regenerated
`sweep.csv` still showed zcring losing to unix at 256 KiB–1 MiB. This is a
*third*, independent cause, not a residue of the first two.

Controlled A/B, 1 MiB, N=1, gap=100 µs, package cool in both cases, no
poller/other interference, 3 reps each:

| condition | zcring p50 |
|---|---|
| C-states enabled | 122.9, 128.4, 133.0 µs |
| C-states disabled | 217.2, 198.8, 202.1 µs |

Clean, tight, ~65% apart, with temperature and offered rate controlled for
in both. pipe/unix are not similarly affected (unix was if anything
slightly *faster* disabled: 152–162 µs either way) — this is specific to
zcring's busy-spin consumer, not a general "disabling idle states costs
everyone" effect. Root mechanism not fully pinned down (candidates: lost
access to a power-efficient wait instruction path on the spinning core, or
turbo/RAPL power-budget effects from the pinned core never truly idling) —
investigated with `turbostat` briefly but decided not worth blocking the
dataset regen on nailing down further. The effect itself is reproducible
and well-isolated from rate and heat; that's enough to report honestly.

**Decision (user, same-day):** don't split methodology by payload size
(C-states enabled for large, disabled for small) — keep C-states disabled
uniformly, since that's the honest, determinism-first configuration this
problem statement actually asks for, and report the large-payload loss as a
disclosed trade-off rather than engineering around it to preserve the old
number. README.md's "Why large payloads lose here" section is the writeup.

**Silver lining, not a consolation prize:** the fan-out data shows the
trade-off is N=1-only. At 1 MiB, zcring loses at N=1 (0.82×) but wins at
N=2 (1.38×) and N=4 (2.03×), because the zero-copy fan-out advantage scales
with consumer count and this C-state cost doesn't. That's arguably a
*stronger* scalability story than the flat 1.47× the old dataset claimed —
it shows a genuine crossover, not just a multiplier.

### Follow-up: busy-spin/power-budget hypothesis tested — NEGATIVE RESULT

Hypothesis (posed independently, same day): the consumer's continuous
busy-spin while waiting — at 1 MiB / gap=100 µs that's ~100 µs of unbroken
max-frequency polling per message — collapses the package's turbo power
budget and constrains uncore/memory-controller frequency, which a
1 MiB memory-bandwidth-bound transfer depends on. Predicts the observed
pattern: zcring penalized, unix (blocking consumer) not, and matches the
earlier thermal finding's asymmetry (zcring degrades ~70% under heat,
unix 0–20%).

Tested it directly. Added `--sleep` to `bench.c` (`wait_backoff_sleep()`,
`bench/bench.c`): a deliberately crude `nanosleep(1µs-requested)` between
polls on the CONSUMER side only, replacing the spin/`--yield` loop, no
futex. If the hypothesis were right, this should recover something close
to the C-states-enabled ~123–133 µs figure despite its own sleep-wake
overhead.

Controlled A/B/C, 1 MiB, N=1, gap=100 µs, cool machine (`wait_for_cool()`
before every rep), C-states disabled, 5 reps each, no poller interference.
Raw data: `results/cstate_mechanism.csv`.

| condition | p50 (ns) | mean (ns) |
|---|---|---|
| zcring, spin (current default) | 197905–203320 | 200121 |
| zcring, `--sleep` (diagnostic) | 222437–227211 | **224052** |
| unix, control | 178722–185002 | 182384 |

**Sleep made it worse, not better.** It did not recover toward 123 µs —
it landed *above* the spin baseline, further from unix than spin was.
**This hypothesis, as tested, is not supported.**

Caveat worth recording rather than glossing over: this test may not
actually isolate what it set out to. Deep C-states are disabled at the OS
level for the pinned CPUs regardless of *why* a core goes idle — whether
because a thread spins-then-yields, blocks in `nanosleep()`, or blocks in
`read()`, the cpuidle framework governing how deep that core can actually
sleep is the same disabled framework in all three cases. So `--sleep` may
not grant the consumer's core any access to power states it didn't already
lack under spin — meaning this test mostly demonstrates that naive
nanosleep-based polling adds its own overhead (~24 µs here, plausibly
nanosleep wake-granularity on a non-RT kernel) without buying anything
back, rather than cleanly falsifying the turbo/power-budget mechanism
itself. A cleaner test would need a real block-and-wake primitive (futex)
to get a qualitatively different relationship with the scheduler than
either spin or nanosleep — which is exactly Layer 2's adaptive
spin-then-futex notification work, not yet built.

**Net status: mechanism still not identified.** Ruled out so far: offered
rate (Open problem #1), thermal throttling in isolation (Open problem #4),
and now naive consumer busy-spin via this negative result. What's shown to
matter: the C-state enable/disable setting itself, cleanly and
reproducibly (Open problem #5 above). Left as an open question for anyone
with more time before the abstract deadline; not blocking, since the
finding is disclosed as a trade-off in README.md regardless of mechanism.
`--sleep` stays in `bench.c` as a diagnostic flag (not used by any
committed sweep) in case someone picks this back up.

## Adaptive notification — what a fresh session needs to know

Built 8 Aug. This is the project's answer to the hackathon's **AI / Technical
Approach** criterion (see `HACKATHON.md` — it is a *core* scored criterion and
the project previously scored zero on it). Frame it as an online-learned
adaptive policy, never as "AI". The submission form's **Model Type** answer is
**Inbuilt Model**: in-house, purpose-built, and justified in the header.

**The claim, in one line:** the spin-then-block threshold is not a constant —
it is learned online, per consumer, from the observed inter-arrival
distribution, by a constrained optimisation with a measured objective.

Design decisions that a later session must not silently undo:

- **The CPU budget β is a policy input; everything else is measured.** That
  split is the whole argument. Do not add a configurable spin constant "for
  convenience" — it re-opens exactly the criticism this work exists to close.
- **The ski-rental floor `S ≥ measured wake cost` is load-bearing.** It is
  what makes fast-arrival traffic stay syscall-free automatically and what
  bounds the learner's worst case by the distribution-free 2-competitive
  policy. Removing it would make the policy pathological at high message rates.
- **Exploration is not decoration.** The greedy policy censors its own
  observations. §8 of the header explains why; a reviewer asking "why is there
  an ε here" needs that answer and it is written down.
- **`wake_ts` in the control block does double duty** — measures the wake cost
  *and* de-censors the arrival sample. Both matter; it is not just telemetry.
- **The wake protocol uses same-location seq_cst RMWs, not fences,
  deliberately.** It was written with `atomic_thread_fence` first — which is
  correct and is what Folly/glibc do — and changed because **GCC's TSan does
  not model `atomic_thread_fence`** (it warns `-Wtsan` and skips it). That
  would have left the most delicate ordering in the framework as the one thing
  `make tsan` could not check. Do not "simplify" the producer's
  `atomic_fetch_add(&waiters, 0, seq_cst)` into an `atomic_load`: the load
  carries no such guarantee and the resulting lost wakeup is a hang, not a
  slowdown. The comment at that line says so.
- **`zc_wake()` is out of line on purpose.** Inlining it pulls
  `zc_now_ns()`'s `struct timespec` into the caller's frame, which under
  `-fstack-protector-strong` (Ubuntu default) adds canary setup to *every*
  `zc_commit` including on non-notify rings. Verified in the generated
  assembly; the non-notify commit is a store plus a predicted branch, exactly
  as before.
- **Notification is opt-in at creation**, so `results/sweep.csv`'s methodology
  is unchanged and still reproducible.

**It demonstrably converges** — `bench --notify` prints the learned state.
At 64 B / 100 µs gap it recovered gap_ewma = 97.8 µs against a true 100 µs,
settled spin at gap/8 (the budget binding exactly as derived), measured the
wake cost at ~2 µs, and explored 0.82% against a nominal 0.78%. At 1 MiB it
learned a 185 µs gap — the *real* inter-arrival, not the 100 µs nominal, since
the producer's own 1 MiB write pass slows it down.

**Honest cost:** at 64 B / 100 µs gap, p50 goes ~128 ns (spin) → ~2.2 µs
(notify). Blocking is not free. Report the two modes as answers to different
questions, never blended.

## Open problem #5, follow-up 2 — busy-spin hypothesis refuted properly (8 Aug)

`reports.txt` §24's `--sleep` test could not settle whether zcring's
busy-spinning consumer causes the ~65% large-payload C-state penalty, because
`nanosleep()` is not a real block/wake cycle either. It named a real futex
block as the test that would. That test now exists.

`scripts/cstate_ab.sh` (committed — this A/B has now been run ad hoc three
times, so it is a script). 1 MiB, N=1, cooled before every invocation, 3 reps,
2×2 over waiter × C-state, unix as control. Run at both rates:

| | gap 100 µs (§22's config) | gap 2000 µs (this dataset's rate) |
|---|---|---|
| spin, C-states on → off | 118.4 → 194.0 µs (+63.9%) | 106.1 → 180.5 µs (+70.1%) |
| notify, C-states on → off | 122.2 → 197.5 µs (+61.6%) | 197.1 → 184.1 µs (−6.6%) |
| unix control, on → off | 157.4 → 174.0 µs (+10.5%) | 243.8 → 154.4 µs (−36.7%) |

Raw: `results/cstate_waiter_ab.csv`, `results/cstate_waiter_ab_rate25.csv`.
Every cell is 3 reps within 1–2 µs.

**NEGATIVE RESULT.** With C-states disabled, zcring costs the same whether the
consumer spins or sleeps (194.0 vs 197.5; 180.5 vs 184.1). The waiter is not
the mechanism. Do not re-run this hypothesis.

**Read the −6.6% carefully — it is not the penalty shrinking.** It is the
C-states-*enabled* baseline getting worse under notify, because at a 2 ms idle
gap a blocking consumer's core enters a deep C-state and its wake pays that
exit latency (~91 µs here). That is a *new*, fully-explained finding, and it
hits the control too: **unix is 36.7% faster with C-states disabled at this
rate.**

**This corrects a claim currently in README/reports:** "pipe/unix are not
affected by the C-state setting" is true at a 100 µs gap and false at 2000 µs.
It was rate-specific and was reported as general. README is fixed; treat any
older statement of it as superseded.

Eliminated so far as causes of the ~65–70%: offered rate (#1), thermal (#4),
busy-spin (this). Remaining candidates are platform properties, not properties
of this code — uncore/memory-controller frequency, or a turbo/RAPL budget an
never-fully-idle package cannot reach. Not worth more time before the
deadline; it is disclosed either way.

## Traps already hit once — do not repeat

- **`--touch` is mandatory.** Without it the harness moves 8 bytes and
  asserts the rest, producing a fictitious flat curve. Cost us one wasted
  sweep.
- **Never pin a consumer to the producer's SMT sibling.** Did this once;
  p50 looked fine while p99 was inflated 185×.
- **Spinning consumers oversubscribe.** At N=4 on 4 hardware threads the
  benchmark measured scheduler thrash, a 40× artifact. `--yield` was the
  stopgap; adaptive futex notification (`--notify`, 8 Aug) is the real fix,
  and `scripts/fanout.sh` now uses it. How much of the artifact it actually
  removes is not yet measured — see the fan-out note at the top of this file.
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

1. ~~Regenerate both datasets at `RATE_FRACTION=25`, REPS=5, C-states
   disabled — baseline and fan-out. Sensitivity check at two other
   fractions. Update README.md tables, delete superseded ones.~~ **Done**,
   same-day follow-up session — see Open problems #1, #4, #5.
2. ~~Demo/pipeline.~~ **Done**, follow-up session — see "Demo pipeline
   results" below.
3. ~~Adaptive spin-then-futex notification.~~ **Done**, 8 Aug — see
   "Adaptive notification" above. It also settled the busy-spin hypothesis
   (Open problem #5, follow-up 2): negative, conclusively.
4. **Regenerate the fan-out sweep under `--notify`.** `results/fanout.csv`
   is now historical (measured with the removed `--yield`) and the fan-out
   crossover claim rests on it. Highest-value measurement outstanding, and
   it doubles as the first test of whether notification removes the 64 B /
   N=4 spinner artifact. *Sonnet* — it is a script run, not a design task.
   Needs a quiet machine and a few hours of thermal-gated wall clock.
5. **Draft the abstract.** Target submission 8–10 Aug; window closes 25 Aug.
   *Opus.* Nothing is blocking this — but the abstract now has a real
   answer for the AI/Technical Approach criterion and Model Type, which it
   did not have before. See "Adaptive notification" above for the framing
   and "The story the abstract should tell" below for the numbers.
6. Optional but recommended: live-USB scaling run on the Ryzen for N=8.
   *Sonnet.* Do this only if the abstract will claim scaling beyond N=2.
7. `eventfd`/`epoll` bridge (the remaining piece of Layer 2 notification);
   producer crash recovery; determinism rigor (isolcpus / nohz_full /
   PREEMPT_RT); iceoryx and ZeroMQ comparators; presentation.

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

**Stale as of the same-day follow-up session — numbers below are current;
do not use anything from before today.**

Syscall-free small-message latency (19.6× at 64 B, 10.0× at 1 KiB) for
embedded real-time control traffic. State the large-payload trade-off
yourself before a judge finds it: at N=1, zcring *loses* to a UNIX socket
at 256 KiB–1 MiB (0.68×–0.84×), a real, disclosed cost of the C-state
configuration the small-message determinism claim requires (see README.md
"Why large payloads lose here", STATUS.md Open problem #5).

Then show the fan-out crossover, which is the more interesting scalability
story precisely because it isn't a flat multiplier: at 1 MiB, the same 0.82×
loss at N=1 becomes a 1.38× win at N=2 and a 2.03× win at N=4, because
publication is one release store regardless of N while copy-based
transports pay a full extra copy per additional consumer. The demo pipeline
(`make demo`, see below) makes this concrete and live rather than only a
table: a real 30fps camera-shaped workload, ten minutes, zero drops, flat
memory, 30.9 GB of traffic avoided.

## Demo pipeline results (`make demo`)

Built same-day, follow-up session. `demo/pipeline.c`: one producer
synthesises 640×480 grayscale frames at 30fps; three consumers
(edge-count, jitter-tracker, checksum) each process every frame in full
(all touch every pixel — no consumer gets to look cheap by ignoring most
of the frame); live terminal dashboard; `--transport=zcring|unix` runs the
identical pipeline over both transports. `scripts/demo.sh` reuses
`check_cstates` from `scripts/lib.sh` and computes CPU pinning the same
way the sweep scripts do.

**Ten-minute validation run, both transports, `DURATION=600`:**

| | zcring | unix (comparator) |
|---|---|---|
| frames published | 18,000 / 18,000 target | 18,000 / 18,000 target |
| average fps | exactly 30.000 (zero drift) | exactly 30.000 (zero drift) |
| drops (any consumer, whole run) | 0 | 0 |
| producer RSS, steady state | 155,808 KB | 2,188 KB |
| consumer RSS, steady state (total, 3 procs) | 461,780 KB (~154,000 each) | 2,996 KB (~999 each) |
| RSS trend over 10 min | flat from ~t=60s (first ring wrap) onward — 12 samples, identical after the first two | flat throughout, all 13 samples identical — no 150 MB ring to page in |
| p50/p99 latency, end of run | edge-count 708/738µs, jitter 119/130µs, checksum 599/631µs | edge-count 598/643µs, jitter 377/443µs, checksum 782/1103µs |
| memory traffic avoided vs sockets | **30.90 GB** over the run | n/a (this is the baseline) |

zcring's jitter consumer is markedly faster than unix's (119µs vs 377µs
p50) despite doing identical work (full-frame min/max scan) — with a
307 KB frame, the kernel-copy cost is a much bigger fraction of a
light-processing consumer's total latency than of a heavy one
(checksum's gap is smaller: 599 vs 782µs), consistent with the syscall/copy
overhead being closer to fixed-cost and the processing being the variable
that dilutes it.

zcring's RSS is dominated by the shared ring (512 slots × ~307 KB frame ≈
150 MiB), fully paged in by every process once the ring has wrapped once
(~17s at 30fps) — that is expected, bounded, shared-mapping behavior, not
a leak, and it is why the per-process RSS is roughly identical across the
producer and all three consumers (they're mapping the same memfd).

**Bug found and fixed during validation, worth remembering:** the first
version hung forever if the producer died any way other than a caught
signal. `SIGKILL`-testing this directly (not just trusting the design)
found it: consumers spun on an empty ring with no way to learn the
producer was gone, because `g_stop` is a per-process flag that a duration-
elapsed exit never sets in the children, and only a caught signal
(SIGINT/SIGTERM) does. Fixed two ways: (1) the producer explicitly sends
SIGTERM to all consumers on any exit path, not just signal-driven ones;
(2) an orphan watchdog in the consumer's wait loop, checked once per
`sched_yield()` — but comparing `getppid()` against the PID captured
*right after fork*, not against the literal value `1`. This environment
reparents orphans to a subreaper rather than to init, so the common
`getppid() == 1` idiom would have silently never fired here. Verified by
`SIGKILL`-ing the producer directly mid-run and confirming all three
consumers exit within seconds, no leaked processes.

Raw soak-test sample logs (RSS + frame count every 45s, both transports,
full 10 minutes) are not committed as CSVs — this is a live-monitoring
tool, not a quoted-latency benchmark — but the methodology and final
numbers above are reproducible via `DURATION=600 make demo` and
`DURATION=600 TRANSPORT=unix make demo`.

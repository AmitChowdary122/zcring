> **Internal working file.** Development context and standing decisions,
> kept so that work carried across sessions stayed consistent. It describes
> how the project was run, not what it does — see `README.md` for that, or
> `EXPLAINER.md` for a plain-language version.

# CONTEXT.md — project context
**Read `docs/dev/HACKATHON.md` first — it holds the official rules, the real submission
form, and the actual evaluation rubric, and it outranks every other file
including this one.** Then this file, which is the summary that keeps a fresh
session from making expensive mistakes. `docs/dev/PLAN.md` has the strategy, `README.md`
the benchmark methodology, `RUNNING.md` the operational detail.

## How this project is worked

Work is split between a planning pass that drafts scope and instructions and
an implementation pass that builds and measures against them. `docs/dev/STATUS.md` is
the authoritative build/measurement state and the handoff point between
sessions — read it first. `vault/` is a local working-memory notebook; it is
gitignored and not part of this repo's public history.

## What this is

A submission to the **SSM / CDAC Next-Gen Kernel Hackathon**, Track 1 (Core),
problem statement: *Zero-Copy Shared-Memory IPC Framework for Embedded Linux*.

The project is `zcring` — a zero-copy shared-memory IPC framework built on a
lock-free MPMC ring over a memfd-backed mapping.

**Participant is solo.** There is no one else to parallelise onto. Scope
decisions must respect a single person's debugging bandwidth.

## Deadlines (today is early August 2026)

| Date | What |
|---|---|
| **25 Aug 2026** | Abstract + problem-statement selection closes. **Real code freeze.** |
| **~23–24 Aug** | **Stage 1 closes.** Portal showed ~400 h remaining on 7 Aug. This is a **full submission**, not an abstract: architecture diagram, PPT, public GitHub link, description/novelty/innovation text, demo video. See docs/dev/HACKATHON.md for every field. |
| **~18–20 Aug** | *Self-imposed* target to have everything uploaded, leaving buffer. |
| **1st week Sep** | Online presentation of the working prototype to judges. This is where the winner is decided. |

The trap in the official schedule: the prototype demo follows the abstract by
about a week, so there is no build window between the two gates. Treat 25 Aug
as the date everything must work by.

## Evaluation rubric

See `docs/dev/HACKATHON.md` for the full rubric (innovation, feasibility, scalability,
impact, AI/technical approach, security, documentation, UX — the architecture
diagram is scored directly). Two criteria shape the architecture here:

- **AI/Technical Approach.** The adaptive spin-then-futex notification
  threshold, learned online from the observed inter-arrival distribution, is
  the project's answer — see docs/dev/STATUS.md "Adaptive notification" and
  `src/zcring.h` §§5–10. It's an in-house, purpose-built online-learned
  policy (Model Type: Inbuilt Model on the submission form), not a bolted-on
  LLM.
- **Security.** Layer 3 (kernel-enforced arbitration) is the response — see
  "Architecture" below.

Fan-out to N consumers, a concrete embedded use case (the camera pipeline
demo), and committed reproducible benchmark data are all core deliverables
here, not nice-to-haves.

## Current state

Layer 1 is **complete and validated**:

- Lock-free MPMC ring (Vyukov sequence scheme) over `memfd_create` + `mmap`.
- `reserve`/`commit` and `acquire`/`release` hand back raw pointers into
  shared memory for in-place construction and reading. No `memcpy` in the data
  path.
- Tests pass: exactly-once delivery across 4 producers × 4 consumers over
  200,000 messages, plus cross-process via inherited memfd.
- **ThreadSanitizer runs clean.** The acquire/release edges are sound. Re-run
  `make tsan` after any change to `src/zcring.h` — this is non-negotiable, as
  memory-ordering bugs are invisible in normal runs.

## The single most important thing to not get wrong

**Always benchmark with `--touch`.**

Without it, the harness stamps and reads only an 8-byte header while the
payload is never actually written or read. zcring then moves 8 real bytes and
asserts the rest exists, while pipe and unix genuinely move all of them. The
result is a flat line — latency apparently independent of payload size — that
is **an artifact of the harness, not a property of the design**.

`scripts/sweep.sh` now passes `--touch` unconditionally. Do not add an escape
hatch.

### Current, defensible numbers

`README.md`'s "Measured Layer 1 results" and "Fan-out results" sections are
authoritative — read those, not this file, for anything quoted externally.
In short: 64 B is 16.8× vs pipe on the shipped code (reproduced across two
independent measurement sessions), large payloads (256 KiB–1 MiB) lose to a
UNIX socket at N=1 under the C-state-disabled configuration the determinism
claim requires (disclosed trade-off, see README's "Why large payloads lose
here"), and fan-out wins that back from N=2 on (0.82× → 1.37× at 1 MiB,
`results/fanout.csv`) — do not claim growth past N=2, see README's
fan-out section for why. p99.9 is quotable at N=1 with deep C-states
disabled (1.04 µs mean); p99.99 is not yet.

Several earlier claims in this file did not survive follow-up measurement
and were retracted rather than quietly replaced — see README.md and
`results/PROVENANCE.md` for what's current. Don't resurrect a superseded
number from git history without re-verifying it.

## Machines

**The measurement laptop died on 14 Aug and was revived on 20 Aug** — the
fault was the drive, the laptop is sound. It is again the canonical
measurement machine, and `results/sweep.csv` was re-measured on it against
current code. `scripts/lib.sh:check_machine()` refuses to write a canonical
dataset filename on a non-i3 CPU; it *passes* on the i3, so `OUT_SUFFIX`
discipline is manual. Full statement in `results/PROVENANCE.md`.

| Machine | Role |
|---|---|
| **Intel i3-1115G4, 2 physical cores + SMT** | **Canonical measurements.** Dead 14 Aug, revived 20 Aug. |
| **Ryzen 9 270, 8C/16T** — Ubuntu SSD, plus WSL2 (Kali) on Windows | Secondary labelled dataset (`OUT_SUFFIX=_ryzen`); development and builds. |

Claims stay anchored to the i3: 2–4 cores is what embedded deployment looks
like, and a Ryzen run answers "do the conclusions survive a different
microarchitecture?" — not "are the numbers bigger".

The measurement box is **dual-core with SMT** (`nproc` reports 4, but
`lscpu` shows 2 cores/socket). Describe it as "dual-core with SMT", never as
quad-core. WSL2 inflates even p50 by ~8–10× on the comparators, so nothing
from that machine is quotable.

WSL2 numbers are not usable for submission — hypervisor jitter inflates p99.9
by orders of magnitude, and `cpupower` / `isolcpus` / `nohz_full` /
PREEMPT_RT are meaningless there.

**4 cores is a feature, not a limitation.** The problem statement is about
*embedded* Linux, where 2–4 cores is typical. Frame it as "measured on a
platform representative of embedded deployment," with the 16-thread Ryzen as
the desktop scaling data point.

### Benchmarking discipline on this machine

- Quiet the machine: close browsers, editors, Docker.
- `sudo cpupower frequency-set -g performance` before any quoted run.
- Pin producer and consumer to **distinct physical cores**, never SMT
  siblings — siblings share L1/L2 and flatter shared-memory IPC dishonestly.
  `scripts/sweep.sh` reads `thread_siblings_list` and picks correctly; verify
  the line it prints.
- `REPS=5` minimum for anything quoted. One run is not a measurement; report
  the spread.
- Commit raw CSVs to the repo. Judges may ask.

## Architecture — layers, cut from the top

Each layer is independently demoable and nothing below depends on anything
above. **Scope is cut from the top, never the middle.**

```
Layer 4 (optional)  dma-buf / hardware buffer sharing
Layer 3 (stretch)   kernel module: access control + doorbell
Layer 2 (core)      adaptive notification, fan-out, crash recovery, epoll bridge
Layer 1 (DONE)      lock-free ring over memfd, in-place construction
```

**Layer 3 is the answer to the hardest question you'll be asked:** *"How is
this not iceoryx?"* iceoryx is pure userspace and needs a central broker
daemon, so it structurally cannot stop a buggy or malicious peer from
corrupting the shared ring. Kernel-enforced arbitration closes exactly that
gap. That reframes the kernel component from extra credit into the novelty
claim — which is why Layer 3 matters more than pure risk-management would
suggest. Still gated on Layers 1–2 being solid.

## Next tasks, in order

1. ~~**Layer 2 — adaptive notification.**~~ **DONE 8 Aug.** Spin-then-futex
   with the budget learned online; `--yield`/`YIELD_SPINS` removed, `--notify`
   replaces them. ABI still v2 (statically asserted). TSan clean. Remaining
   piece of this item: the `eventfd` bridge for `epoll` composition.
2. **Regenerate the fan-out sweep under `--notify`.** Removing `--yield` made
   `results/fanout.csv` historical — still valid as measured, no longer
   reproducible, and it is what the fan-out crossover claim rests on. Highest-
   value measurement outstanding. See docs/dev/STATUS.md.
3. ~~**Layer 2 — fan-out.**~~ **DONE.** One producer, N consumers,
   per-consumer cursors, zero copies for all.
4. **Layer 2 — producer crash recovery.** A producer dying mid-`reserve`
   currently leaks that slot. Needs stale-slot reclamation with bounded
   recovery time. (Consumer death is already handled by `zc_bcast_reap()`.)
5. **Determinism rigor.** p99/p99.9/p99.99 histograms, jitter under
   `stress-ng`, `isolcpus`, PREEMPT_RT if feasible. This is the top
   differentiator — the statement asks for *deterministic* communication and
   most teams will report means.
6. **Comparators.** Add iceoryx and ZeroMQ to the sweep. Benchmarking against
   iceoryx rather than only pipes signals awareness of the actual field.
7. **Demo + presentation.** Reserved for the final week. Do not spend it
   coding.

## Persisting state across sessions

No model remembers anything between sessions — the repo is the only memory.
`docs/dev/STATUS.md` holds current state, open problems, decisions already made, and
traps already hit; update it at the end of any working session, since a
result reported only in chat is lost the moment the session closes. Design
rationale goes in the relevant header, not in chat — `src/zcring.h` is the
model to follow. `git pull` before starting work, `git push` when stopping.

## Working conventions

- C11, `-Wall -Wextra`, keep the build **warning-free**.
- Fast-path operations live in `zcring.h` and must stay inlineable.
- `make test` before every commit; `make tsan` after every ring change.
- Commit raw benchmark CSVs alongside code.
- All submissions must be original — iceoryx/ZeroMQ appear **only as
  benchmark comparators**, never as incorporated code. Keep that boundary
  explicit in the repo and the slides.

## Standing principles

1. Cut from the top, never the middle. Complete Layers 1–2 beats broken 1–4.
2. A measured number beats a claimed one, everywhere.
3. The presentation is the product. Code that can't be shown in ten minutes
   did not happen.
4. Acknowledge limitations before judges find them — it reads as rigor.
5. Score against the rubric, not against your own taste.

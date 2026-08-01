# CLAUDE.md — project context

Read this first. `PLAN.md` has the full strategy, `README.md` the benchmark
methodology, `RUNNING.md` the operational detail. This file is the summary
that keeps a fresh session from making expensive mistakes.

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
| **~8–10 Aug** | Target for submitting the abstract — early, backed by measured data |
| **1st week Sep** | Online presentation of the working prototype to judges. This is where the winner is decided. |

The trap in the official schedule: the prototype demo follows the abstract by
about a week, so there is no build window between the two gates. Treat 25 Aug
as the date everything must work by.

## Evaluation rubric — this drives priorities

Judged on **innovation, feasibility, scalability, impact**. Performance and
technical depth are *not* named criteria. A benchmark number only scores
insofar as it evidences one of those four words.

Practical consequences:

- **Fan-out to N consumers is a core deliverable, not a nice-to-have.** It is
  the only work item that speaks directly to *scalability*.
- **A concrete embedded use case matters more than microbenchmarks.** "IPC is
  faster" scores weakly on *impact*; "this camera → inference → display
  pipeline is impossible without it" scores well.
- **Reproducibility counts as *feasibility*.** Pinned environments, scripted
  benchmarks, and committed raw data are worth real points.

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
result is a beautiful flat line — latency apparently independent of payload
size — that is **an artifact of the harness, not a property of the design**.
One judge asking "does your consumer ever touch the data?" destroys it, and
takes the credibility of everything adjacent with it.

`scripts/sweep.sh` now passes `--touch` unconditionally. Do not add an escape
hatch.

### Claims that are defensible (measured bare metal, 1 Aug 2026)

- Removes the **syscall from the data path** entirely.
- Eliminates **two of four memory passes**, but note this does *not* yield 2×
  at large payloads — see below.
- **64 B: 18.8× vs pipe. 1 KiB: 7.8×. 4 KiB: 3.5×.** Small-message latency is
  the real story, and it is the regime embedded real-time control lives in.
- **Large payloads converge to 1.1–1.5× vs a UNIX socket**, because at that
  point you are memory-bandwidth bound and the kernel's `memcpy` is very well
  optimised.

**An earlier version of this file claimed a "~2× floor" at 1 MiB. That came
from VM data and is wrong — bare metal measures 1.1×.** Do not reinstate it.

**Lead with small-message latency, not with a bandwidth multiplier.** State
the large-payload convergence yourself before a judge finds it; then show
fan-out, which is where the large-payload case is genuinely won (N consumers
= N copies avoided, so the advantage grows linearly with N).

**p99.9 is not quotable yet** — varies 1000× across identical reps due to OS
scheduling noise on a non-isolated kernel. Needs the isolcpus/RT work first.

## Machines

| Machine | Role |
|---|---|
| Ryzen 9 270, 8C/16T, **WSL2 (Kali)** | Development. Fast builds. |
| **Intel i3-1115G4, 2 physical cores + SMT, bare-metal Ubuntu** | **All quoted measurements.** |

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

1. **Layer 2 — adaptive notification.** Spin on the sequence counter for N ns,
   then `FUTEX_WAIT`; producer issues `FUTEX_WAKE` only when a waiter flag is
   set. Fields `futex_word` and `waiters` already exist in `zc_ctrl_t` so the
   shared ABI does not change. Also: `eventfd` bridge for `epoll` composition.
   - Note: the current benchmark's zcring consumer **spins** while pipe/unix
     consumers block, so idle CPU cost is not yet comparable. This is the
     honest fix for that asymmetry.
2. **Layer 2 — fan-out.** One producer, N consumers, per-consumer cursors,
   each reading the same buffer with zero copies. Rubric-critical.
3. **Layer 2 — crash recovery.** A producer dying mid-`reserve` currently
   leaks that slot. Needs stale-slot reclamation with bounded recovery time.
4. **Determinism rigor.** p99/p99.9/p99.99 histograms, jitter under
   `stress-ng`, `isolcpus`, PREEMPT_RT if feasible. This is the top
   differentiator — the statement asks for *deterministic* communication and
   most teams will report means.
5. **Comparators.** Add iceoryx and ZeroMQ to the sweep. Benchmarking against
   iceoryx rather than only pipes signals awareness of the actual field.
6. **Demo + presentation.** Reserved for the final week. Do not spend it
   coding.

## Shared memory across machines and sessions

This project runs on two machines (Windows/Cowork for planning and docs,
bare-metal Ubuntu/Claude Code for building and measuring) and across many
sessions. **No model remembers anything between sessions. The repo is the
only memory.**

`STATUS.md` is that memory. It holds current state, open problems, decisions
already made, and traps already hit. It is read by whichever machine picks
the work up next.

Obligations, both machines:

- `git pull` **before** starting work. `git push` **when stopping.**
- **Update `STATUS.md` at the end of any working session** — findings,
  decisions, anything discovered that is not obvious from the code. A result
  reported only in chat is lost the moment the session closes. This has
  already nearly cost us the C-state finding and the offered-rate problem.
- Design rationale goes in the relevant header, not in chat. `src/zcring.h`
  is the model to follow.
- In the Windows working tree, stage files **by name**. Never `git add -A`
  there: it only ever holds current doc edits, everything else in it is stale,
  and a blind `-A` once reverted a Makefile fix and clobbered a dataset.

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

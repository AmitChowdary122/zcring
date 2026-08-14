# CLAUDE.md — project context

**Read `HACKATHON.md` first — it holds the official rules, the real submission
form, and the actual evaluation rubric, and it outranks every other file
including this one.** Then this file, which is the summary that keeps a fresh
session from making expensive mistakes. `PLAN.md` has the strategy, `README.md`
the benchmark methodology, `RUNNING.md` the operational detail.

## How this project is worked — read before doing anything

Two tools, two jobs. **Cowork plans; Claude Code executes.** The user runs a
Cowork session to decide *what* to do and to draft the instruction, then hands
that instruction to Claude Code, which does the building and measuring. Cowork
does not write kernel code or run benchmarks; Claude Code does not decide
scope or wording.

If you are **Cowork**, at the start of every session:

1. `HACKATHON.md` — only if the task touches rules, dates or the form.
2. **`vault/INDEX.md`** — the working-memory index, ~90 lines. It is the
   cheapest way to find out what has already been decided, measured,
   retracted, or got wrong once. Open only the notes your task names; do not
   bulk-read it.
3. `STATUS.md` — current build and measurement state. Authoritative over the
   vault whenever they disagree.

Then produce an instruction precise enough to hand over, and **say which model
to use** (Sonnet 5 by default; Opus 5 for `src/zcring.h`, kernel code, and
strategy or writing work).

If you are **Claude Code**: read `STATUS.md`, then whatever design doc the
instruction names. The `vault/` directory is Cowork's notebook — you may read
it, but `STATUS.md` is the contract between machines and the place your
findings must be written back to.

**`vault/` is committed but must be untracked before the repo is made public
at submission** — `git rm -r --cached vault/`. See `.gitignore`.

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
| **~23–24 Aug** | **Stage 1 closes.** Portal showed ~400 h remaining on 7 Aug. This is a **full submission**, not an abstract: architecture diagram, PPT, public GitHub link, description/novelty/innovation text, demo video. See HACKATHON.md for every field. |
| **~18–20 Aug** | *Self-imposed* target to have everything uploaded, leaving buffer. |
| **1st week Sep** | Online presentation of the working prototype to judges. This is where the winner is decided. |

The trap in the official schedule: the prototype demo follows the abstract by
about a week, so there is no build window between the two gates. Treat 25 Aug
as the date everything must work by.

## Evaluation rubric — this drives priorities

**Superseded — see `HACKATHON.md` for the real, much more detailed rubric.**
The four-word summary below was an early approximation; the actual criteria
add *AI/Technical Approach*, *Security*, *Documentation Quality*, *User
Experience*, and score the **architecture diagram itself**. Two consequences
worth carrying here:

- **AI/ML is a CORE criterion. As of 8 Aug the project has an answer.** The
  adaptive spin-then-futex threshold, learned online from the observed
  inter-arrival distribution, is **built** — see STATUS.md "Adaptive
  notification" and `src/zcring.h` §§5–10. Model Type on the form is
  **Inbuilt Model**. Frame it as an online-learned adaptive policy, never as
  "AI". Do not bolt on an LLM. See HACKATHON.md.
- **Security is scored**, which promotes Layer 3 kernel arbitration: it now
  scores on both *Novelty* and *Security*.

Earlier approximation, still directionally useful: judged on **innovation,
feasibility, scalability, impact**; a benchmark number only scores insofar as
it evidences one of them.

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

### Claims that are defensible (measured bare metal, gated regen, 1 Aug 2026)

- Removes the **syscall from the data path** entirely.
- Eliminates **two of four memory passes**, but note this does *not* yield 2×
  at large payloads — see below.
- **64 B: 18–20× vs pipe. 1 KiB: ~9.5–10×. 4 KiB: 3.66× (conservative; a
  cleaner re-measurement gives 4.14×).** Small-message latency is the real
  story, and it is the regime embedded real-time control lives in.
  **Quote 64 B as a range, not as 19.6×** — the full suite was re-measured on
  a separate occasion and the comparators came out 5–12% faster at small
  payloads, giving 18.23× rather than 19.62×. zcring itself reproduced within
  2% everywhere. A range across two sessions is the defensible claim.
- **Large payloads (256 KiB–1 MiB) LOSE to a UNIX socket at N=1** (0.68×–
  0.84×), under the C-state-disabled configuration this project requires for
  the small-message determinism claim below. Confirmed independent of
  thermal state, offered rate, **and (8 Aug) of whether the consumer spins or
  genuinely blocks on a futex** — a real, disclosed trade-off, not a bug or
  measurement artifact. See README.md's "Why large payloads lose here".
  **Superseded sub-claim:** "pipe/unix are not affected by the C-state
  setting" was rate-specific and is now known to be false at low message
  rates — at a 2 ms gap, unix is 36.7% *faster* with C-states disabled,
  because a blocking consumer's core pays idle-state exit latency on wake.
  Do not repeat the old general form.
- **Fan-out wins the large-payload case back from N=2 on.** At 1 MiB:
  **0.82× at N=1, 1.36× at N=2** under the shipping `--notify` waiter
  (`results/fanout_notify.csv`). A genuine crossover — publication is O(1) in
  N, copying transports are O(N).
  **Do NOT claim growth past N=2.** The old 2.03× at N=4 was measured under
  `--yield`, a flag that no longer exists; under `--notify` N=4 gives 1.24×
  because `FUTEX_WAKE` makes four consumers runnable at once on two physical
  cores and they thunder (64 B N=4: 1.03 µs → 12.3 µs). That is
  oversubscription, not a transport property.
  **Do not chase this with a bigger machine.** One producer plus four
  consumers is oversubscribed on a 4-core embedded target too — every
  consumer runs on every message, so consumer count is bounded by core count
  on any platform. That is a deployment property, not a measurement defect.
  The crossover sits at **N=2**, which fits on every embedded part there is,
  and this hardware measures it fine. A prettier N=8 number from a desktop
  CPU would be *less* representative of the problem statement, not more.
- **Small payloads lose their entire advantage under `--notify`** (18.3× →
  0.93× at 64 B) because the syscall comes back. This is derived, not a bug —
  see `zcring.h` §5. **The 18–20× headline is a dedicated-core (spin) number
  and must be stated as such.** `sweep.sh` defaults to `WAITER=spin` for
  exactly this reason.

**Two earlier versions of this file made claims that did not survive
follow-up measurement.** First a "~2× floor" at 1 MiB (VM data, wrong on
bare metal). Then the "1.1–1.5× large-payload win" that replaced it turned
out to be measured under a C-state configuration that predates and
conflicts with the tail-latency fix below — under the correct,
determinism-first configuration, large payloads *lose* at N=1. Do not
reinstate either. Current numbers come from `results/sweep.csv` /
`results/fanout.csv`, regenerated with thermal gating and a justified
per-size offered rate (`RATE_FRACTION`) — see README.md for the full
methodology and sensitivity check.

**Lead with small-message latency, not with a bandwidth multiplier.** State
the large-payload trade-off yourself before a judge finds it; then show
fan-out, which is where the large-payload case is genuinely won back (N
consumers = N copies avoided, so the advantage grows with N and crosses
from a loss at N=1 to a 2×+ win by N=4).

- **The whole dataset was re-measured on a separate occasion** into
  `results/{sweep,fanout}_verify.csv`, from a clean boot with the machine
  re-quieted from scratch. The fan-out crossover reproduced within 1.5% at
  every point (0.82/1.38/2.03× → 0.82/1.38/2.06× at 1 MiB). This is
  *feasibility* evidence in rubric terms and should be said out loud — almost
  no competing submission will have re-measured itself. See README.md's
  "Reproducibility" section.

**p99.9 is quotable at N=1 with deep C-states disabled** (1.04 µs mean,
sub-2 µs range — see README.md). p99.99 is not yet; still shows occasional
excursions from a smaller, separate noise source (IRQ/scheduling jitter),
which the isolcpus/nohz_full/PREEMPT_RT work is meant to close.

## Machines

**The measurement laptop died on 14 Aug 2026.** Its SSD was moved into the
Ryzen machine and now dual-boots there. Every committed number came from
hardware that no longer exists: valid as measured, **not reproducible by us on
demand**. `scripts/lib.sh:check_machine()` refuses to write a canonical
dataset filename on a non-i3 CPU — do not defeat it. Full statement in
`results/PROVENANCE.md`.

| Machine | Role |
|---|---|
| **Intel i3-1115G4, 2 physical cores + SMT** | **All quoted measurements. DEAD 14 Aug.** |
| **Ryzen 9 270, 8C/16T** — Ubuntu SSD as dual boot, plus WSL2 (Kali) on Windows | The only machine now. Development, builds, the Layer 3 VM. |

The claims stay anchored to the i3 regardless. A dead laptop does not change
which platform the claims should be *about*, and 2–4 cores is what embedded
deployment looks like. Any Ryzen run is a secondary, labelled dataset
(`OUT_SUFFIX=_ryzen`), and its value is answering "do the conclusions survive
a different microarchitecture?" — not "are the numbers bigger".

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
   value measurement outstanding. See STATUS.md.
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

# Stage 1 submission form — field text

Every field below is drafted from `results/*.csv` and README.md's authoritative
tables. Note two portal constraints found on submission: **Description and
Novelty are capped at 3000 characters**, and the validator **rejects angle
brackets as HTML tags** (so write header names without them).

Every number below traces to committed raw data. Provenance caveats are in
`results/PROVENANCE.md`.

---

## Title

> **zcring — Deterministic Zero-Copy Shared-Memory IPC for Embedded Linux**

*Alternate, if a subtitle field exists:* "A lock-free shared-memory transport
with an online-learned wake policy."

---

## Problem Statement

*(fixed by the organisers — reproduce verbatim)*

> Zero-Copy Shared-Memory IPC Framework for Embedded Linux — Develop a
> zero-copy shared-memory IPC framework for Embedded Linux to reduce latency,
> eliminate unnecessary memory copies, and provide deterministic,
> high-performance communication for real-time embedded applications.

---

## Objective

> To build and measure a shared-memory IPC transport for embedded Linux in
> which application messages are constructed and read **in place**, so that no
> copy of the payload is made anywhere in the data path and no system call is
> issued per message.
>
> The target property is **determinism, not peak throughput**: real-time
> embedded workloads are constrained by worst-case latency rather than by
> average latency, so the objective is a transport whose tail behaviour is
> bounded and explainable, on hardware representative of embedded deployment
> (2–4 cores), with every claim traceable to committed raw measurement data.

---

## Description

> A camera on an embedded device produces frames that several processes must
> each read — one detects edges, one checksums, one tracks timing. Over a
> UNIX socket the frame is copied into the kernel and out again *for every
> consumer*. zcring publishes it once and all consumers read the same bytes.
> The committed demo runs exactly this pipeline over both transports:
> 1800 frames, three consumers, zero drops, **3.09 GB of memory traffic
> avoided** in sixty seconds.
>
> **Mechanism.** zcring is a lock-free multi-producer/multi-consumer ring
> (Vyukov sequence scheme, C11 acquire/release, cache-line padded) over a
> `memfd_create` + `mmap(MAP_SHARED)` region. `reserve`/`commit` on the
> producer and `acquire`/`release` on the consumer return **raw pointers into
> shared memory**, so the application constructs and reads the message in
> place. There is no `memcpy` in the data path: a pipe's four passes over the
> payload become two, and the per-message system call disappears entirely.
>
> **Measured** on bare-metal Ubuntu, Intel i3-1115G4 (dual-core with SMT —
> deliberately embedded-class hardware), `--touch` mode so both sides
> genuinely write and read every payload byte, producer and consumer pinned to
> distinct physical cores, mean of 5 repetitions:
>
> | payload | zcring p50 | best comparator | ratio |
> |---|---|---|---|
> | 64 B | 130 ns | 2.2 µs (pipe) | **16.8×** |
> | 1 KiB | 254 ns | 2.3 µs (pipe) | 9.11× |
> | 4 KiB | 677 ns | 2.7 µs (pipe) | 4.04× |
> | 1 MiB | 183 µs | 153 µs (UNIX socket) | 0.84× |
>
> These are the numbers **the committed code produces**, re-measured on
> 20 August. Earlier drafts of this project quoted 19.6× at 64 B; that figure
> came from the Layer 1 revision (commit `847bee2`, 1 Aug), and a subsequent
> code change cost roughly 16 ns per small message. Rebuilding the old binary
> on the same machine in the same session returned 114 ns exactly, which is
> how the regression was distinguished from a measurement artifact. The old
> dataset is preserved as `sweep_layer1_historical.csv`. We quote what a
> reader who clones the repo will actually reproduce.
>
> **A limitation is stated here rather than left to be discovered.** The
> small-message figures are *dedicated-core* numbers: the consumer spins. If
> it blocks instead, the system call returns to the data path and 64 B falls
> to parity (0.93×). Real-time control loops, software radio and industrial
> controllers routinely dedicate a core, and the framework supports both
> postures — but the number must always be quoted with its posture.
>
> **A second limitation was disclosed, investigated, and then resolved by
> measurement.** On the Intel part, single-consumer transfers above 256 KiB
> were *slower* than a UNIX socket (0.68–0.84×). Four hypotheses were tested
> against that gap — offered rate, thermal state, busy-spin power draw, and
> TLB pressure via huge pages — and **all four were negative**, leaving
> platform power/frequency behaviour as the only remaining candidate. That
> prediction was then tested directly by re-running the identical sweep on a
> different microarchitecture (AMD Zen, `results/sweep_ryzen.csv`, 5
> repetitions):
>
> | payload | Intel i3-1115G4 | AMD Ryzen 9 270 |
> |---|---|---|
> | 64 B | 16.8× | **32.0×** |
> | 1 KiB | 9.11× | 19.7× |
> | 4 KiB | 4.04× | 8.75× |
> | 64 KiB | 1.00× | 4.13× |
> | 256 KiB | **0.68×** | **3.62×** |
> | 1 MiB | **0.84×** | **3.08×** |
>
> Both columns are the same binary, measured within hours of each other — so
> this is a clean cross-microarchitecture comparison rather than two datasets
> from different revisions.
>
> The loss **inverts**. At 1 MiB zcring's p50 falls from 183 µs to 44 µs while
> the UNIX socket barely moves (155 µs → 136 µs), and the p50 reproduces to
> three significant figures across all five repetitions.
>
> Stated precisely, because the distinction is the point: this is **strong
> evidence, not proof**. It is not a controlled A/B: the two parts differ in
> per-core memory bandwidth, fabric clock and prefetch behaviour independently
> of anything tested. What
> the result supports is that **the cost tracks platform memory and frequency
> behaviour rather than the transport design** — four code-side explanations
> were each tested and failed, and the one remaining class of explanation made
> a prediction that independent hardware bore out.
>
> The headline claims in this submission remain anchored to the Intel part,
> because 2–4 cores is representative of embedded deployment and the Zen part
> is a desktop-class CPU. The Zen dataset is secondary and labelled. Its
> *tail* figures are explicitly not quoted: p50 reproduces perfectly there,
> but p99.99 is bimodal across repetitions, which is external interference on
> a machine that was not fully quieted rather than a property of the
> transport.
>
> **Scalability** recovers the large-payload case. Publication costs one
> release store regardless of consumer count, while copying transports pay N
> copies in and N out. At 1 MiB the crossover is at **N=2 (1.37×)**. Growth
> beyond that is deliberately *not* claimed: in broadcast mode every consumer
> runs on every message, so consumer count is bounded by core count on any
> platform, and one producer plus four consumers oversubscribes a four-core
> embedded target exactly as it does the measurement machine.
>
> **Determinism.** Tail latency was traced to a concrete hardware cause —
> deep CPU idle-state exit latency, measured at 1048 µs on this part — and
> eliminated. With deep C-states disabled, p99.9 falls to 1.04 µs and, more
> importantly, its *variance collapses*: before, p99.9 depended on whether the
> idle governor happened to choose C3 during that run. The effect has since
> been confirmed cross-vendor on AMD (350 µs C3 exit), so it is a property of
> the platform class rather than an Intel quirk.
>
> **The zero-copy claim is demonstrated, not asserted.** `perf` profiles of
> both transports at 64 KiB are committed (`results/perf_proof.txt`): the
> UNIX-socket control shows `_copy_to_iter` at 7.8% and `_copy_from_iter` at
> 2.8%, while zcring's profile contains **no copy symbol in 88 symbol rows**.
> The control is what makes it a proof rather than an absence — the method
> demonstrably detects a real copy, then does not find one on our path.
>
> **Robustness.** A consumer that dies is evicted so it cannot gate the ring
> forever; a producer killed *between* `reserve` and `commit` no longer leaks
> its slot, in both unicast and broadcast mode. Both are verified by tests
> that actually `SIGKILL` a peer rather than simulating the condition, with
> measured recovery of 324 ns and 452 ns. Distinguishing a dead peer from a
> merely descheduled one is the harder half and is tested separately, since
> reclaiming from a live producer would corrupt the ring.
>
> **Correctness and reproducibility.** Exactly-once delivery is verified
> across 4 producers × 4 consumers over 200,000 messages and across process
> boundaries; ThreadSanitizer runs clean on both x86 microarchitectures
> tested. The entire benchmark suite was **re-measured on a separate occasion
> from a clean boot** and reproduced within 1.5%. All raw CSVs, the harness,
> and the analysis scripts are committed. Three measurement artifacts found
> during development — a harness that never touched the payload, SMT-sibling
> pinning, and thermal throttling — are documented alongside the results
> rather than omitted, as are several claims that were measured, retracted and
> corrected.

---

## Novelty

> **1. The wake threshold is learned online rather than configured.** Every
> shared-memory transport must decide how long a waiting consumer spins before
> blocking. That value is normally a compiled-in constant, which is a guess
> about a distribution the framework cannot know at build time. zcring learns
> it per consumer from the observed inter-arrival distribution, as a
> constrained optimisation — minimise wake latency subject to a CPU budget —
> using a Robbins–Monro quantile update, a floor derived from the *measured*
> block-and-wake cost by the classical ski-rental argument, ε-greedy
> exploration, and de-biasing of censored samples via a producer-side
> timestamp. The full
> derivation is in `src/zcring.h` §§5–10 and the policy is shown tracking a
> mid-run distribution shift in `docs/adaptive_trace.pdf`.
>
> **2. Kernel-enforced arbitration, and why it is not iceoryx.** Existing
> zero-copy frameworks are pure userspace: every participant maps the segment
> read-write because the ring's own bookkeeping requires it, so a buggy or
> malicious peer can corrupt every other peer. A broker daemon can decide who
> *attaches*; it cannot revoke a page the MMU has already been told is
> writable. Only the kernel can hand out a mapping that is physically unable
> to write. `docs/LAYER3_DESIGN.md` specifies that layer — misc device,
> per-role `mmap` protections, threat model, phased scope. **It is designed
> and specified, not built**, and is presented as such.
>
> **3. The security property comes from the topology, not from shared
> memory.** Full write-protection is possible in broadcast mode and impossible
> in unicast MPMC: a broadcast consumer only advances its own cursor and never
> mutates shared state, whereas a unicast consumer must write slot sequence
> numbers to release slots. One-writer-many-readers is therefore what *makes*
> kernel-enforced isolation possible. This ties the security layer to the
> fan-out design rather than bolting it on, and it bounds the claim honestly.

---

## Innovation

> **True in-place construction.** The API hands the application raw pointers
> into shared memory rather than copying into a staging buffer — genuinely
> zero-copy rather than one-copy renamed. The proof obligation is met by
> benchmarking in `--touch` mode, where both sides write and read every
> payload byte; without it the harness moves only an 8-byte header and
> produces a flat latency curve that is an artifact rather than a result.
> That trap was hit, diagnosed and permanently closed in the harness.
>
> **A hardware cause found for a software-looking problem.** Tail latency was
> not smoothed away or averaged out; it was traced to CPU idle-state exit
> latency, quantified per state from `sysfs`, eliminated, and then confirmed
> on a second vendor. The determinism claim rests on an identified mechanism.
>
> **Portability treated as a correctness property, not a build concern.**
> Every `_Atomic` field in the shared control block is statically asserted
> always-lock-free. If any were not, the compiler would fall back to
> libatomic's lock table — which is keyed by address and is **per-process** —
> so two processes mapping the same ring would serialise against two locks
> that know nothing about each other. That is not a slower ring but an
> unsynchronised one, and no single-process sanitiser run would catch it. The
> cache-line constant is likewise architecture-aware, since aarch64 commonly
> reports 128-byte coherence granules where a hardcoded 64 would silently
> reintroduce false sharing between consumer cursors.
>
> **Measurement discipline as a deliverable.** Benchmarks run at a stated,
> sensitivity-tested offered rate rather than an incidental one; scripts
> refuse to write a canonical dataset filename on a CPU other than the one the
> committed data came from; raw CSVs are committed alongside the code; and
> claims that did not survive re-measurement were retracted in place with the
> reason recorded rather than quietly replaced.

---

## Tech Stack

> **Language:** C11 (`-Wall -Wextra`, warning-free), no third-party runtime
> dependencies.
> **Kernel interfaces:** `memfd_create`, `mmap(MAP_SHARED)`, `futex`,
> `MFD_HUGETLB` / `MADV_HUGEPAGE`, `sched_setaffinity`, `sysfs` cpuidle and
> CPU-topology interfaces.
> **Concurrency:** C11 `<stdatomic.h>` acquire/release ordering; Vyukov
> bounded MPMC queue; cache-line padding via `_Alignas`.
> **Verification:** ThreadSanitizer, a 919-line test suite (exactly-once under
> 4×4 contention, cross-process, notification races, huge-page fallback),
> static layout and lock-freedom assertions.
> **Benchmarking & tooling:** GNU Make, Bash, Python 3 (matplotlib) for
> figures generated from committed CSVs; `cpupower`, `perf`.
> **Comparators:** `pipe(2)` and AF_UNIX sockets, implemented in the harness.
> Third-party frameworks are referenced only as prior art, never incorporated.
> **Platforms:** x86-64 (Intel Tiger Lake, AMD Zen); cross-compiled clean for
> aarch64 and riscv64.

---

## Model Type

> **Inbuilt Model**

*Justification, if a free-text field accompanies it:*

> An in-house, purpose-built online-learned policy rather than a pre-trained
> or third-party model. The spin-then-block threshold is estimated at runtime
> from the observed inter-arrival distribution by a constrained optimisation
> with a measured objective, a floor derived from the classical ski-rental
> argument, and ε-greedy exploration to de-censor its own observations. The
> block-and-wake cost it optimises against is *measured*, not assumed. The
> full derivation is in `src/zcring.h` §§5–10 and the policy is shown tracking
> a mid-run distribution shift in `docs/adaptive_trace.pdf`. No external
> dataset or pre-trained model is used.
>
> **The claim is deliberately narrow, and the limit was measured rather than
> assumed.** A counterfactual replay against the committed traces — scoring
> every fixed budget on the identical arrival sequence — shows that on every
> workload our harness can generate, a *well-chosen constant matches or beats*
> the learned budget. The reason is regime: the measured wake cost is 2.27 µs
> while the achievable median inter-arrival floors at 52 µs, so gaps run
> 23–110× the wake cost, and there blocking immediately is optimal. The policy
> earns its keep only where inter-arrival approaches the wake cost (≈100 kHz
> and above — high-rate IMUs, SDR streams, tight control loops), a regime this
> harness cannot produce. So what is claimed is that the parameter is *derived
> from measurement rather than configured*, and that it tracks a changing
> arrival process; superiority over a well-chosen constant is predicted by the
> derivation and **not** demonstrated here. The replay is reproducible from
> the committed CSVs.

---

## Data Set Used

> Not applicable. No external dataset is used. The adaptive policy learns
> online from runtime inter-arrival measurements taken by the consumer itself.
> All benchmark data is generated by the committed harness (`bench/bench.c`,
> `scripts/`) and the raw CSVs are committed under `results/` with provenance
> recorded in `results/PROVENANCE.md`.

---

## GitHub Link

> `https://github.com/AmitChowdary122/zcring`

**Must be public before submitting.** Untrack the working vault first:
`git rm -r --cached vault/`, re-add `vault/` to `.gitignore`, commit.

---

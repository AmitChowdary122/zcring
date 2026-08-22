> **Internal working log.** The original month-long plan, kept unedited
> so the delivered scope can be compared against the intended scope.
> Several items here were deliberately cut; see `README.md` for what
> actually exists.

# Zero-Copy Shared-Memory IPC Framework — Master Plan

**Hackathon:** SSM / CDAC — Next-Gen Kernel Hackathon
**Track:** 1 (Core) — *Zero-Copy Shared-Memory IPC Framework for Embedded Linux*
**Team:** solo
**Plan date:** 31 Jul 2026

---

## 1. The timeline, corrected

The published schedule hides a trap:

| Milestone | Dates | What it actually means |
|---|---|---|
| Registration + problem selection + abstract | 28 Jul – 25 Aug | Gate 1. Paper only. |
| Online presentation of **working prototype** | 1st week of Sep | Gate 2. **This is where the winner is decided.** |
| Grand finale | Post-evaluation | Scores tallied. |

The trap: the abstract closes 25 Aug and you present a *working prototype* roughly one week later. There is no build window between the two gates. **25 Aug is the real code freeze**, not the start of work.

Three consequences that shape everything below:

1. **Register now, not near the deadline.** Registration and abstract share a window, but nothing rewards waiting. Do it in the first days.
2. **Submit the abstract early (~8–10 Aug), backed by real measurements.** Most abstracts are speculative prose. One containing a real latency graph from a working prototype is a different category of document, and it costs nothing extra because you need the prototype anyway.
3. **The deliverable is a presentation, not a codebase.** Nobody reads 5,000 lines. The code must exist, run, and survive questioning — but the artifact being judged is a ~10–15 minute online demo. Budget for it accordingly (see §6).

Working budget: ~5 weeks at 4–6 h/day ≈ **150–200 hours**. That is enough for something genuinely deep, and more than enough to over-scope into failure. The plan below is structured so that scope is cut from the *top*, never the middle.

---

## 2. Why this problem statement

This statement is the one where **you own the baseline**. You are not trying
to beat tuned kernel code; you are building a framework whose advantage over
`pipe(2)` is structural — you deleted two memory copies, and no amount of
tuning recovers those. The win is guaranteed by construction, which is a rare
property in a hardware/timeline-constrained solo project.

---

## 2a. What the rules change

### "Projects will be evaluated based on innovation, feasibility, scalability, and impact."
Raw performance and technical depth are not named criteria on their own —
benchmark rigor matters insofar as it evidences one of the four words above.
Every major work item is mapped to a criterion in §3a; work that maps to
none gets cut.

### "Once a problem statement is finalized, it cannot be changed."
No pivoting after commitment. This raises the value of Week 1's exit
criterion — get real numbers *before* the abstract locks you in.

### Originality
All submissions must be original and plagiarism-free. iceoryx, ZeroMQ, and the rest appear **only as benchmark comparators** — never as incorporated code. Keep the boundary between your implementation and third-party libraries explicit in both the repo and the slides, and check licences on anything you link against.

### Team size
Rules permit 1–5 members; solo is allowed and is what this plan assumes.

---

## 3. Priorities, in descending order of payoff

### 3.1 Determinism, not throughput — the headline
The problem statement says *"deterministic, high-performance communication for real-time embedded applications."* Reporting **p99 / p99.9 / p99.99 latency and jitter histograms**, not just mean latency and throughput, answers the statement as literally written.

### 3.2 The flat-vs-linear graph
Copy-based IPC latency grows linearly with payload size. True zero-copy latency is **flat**. Plotting both on the same axes across a 64 B → 4 MB payload sweep communicates the entire contribution in one graph.

### 3.3 The notification problem — where the engineering shows
Zero-copy data transfer is the easy half. The hard half is waking the consumer *without a syscall on the fast path*. The adaptive answer: spin on the sequence counter for N nanoseconds, then fall back to `FUTEX_WAIT`; the producer issues `FUTEX_WAKE` only when a waiter flag is set.

### 3.4 Robustness — what makes it deployable rather than a toy
What happens when a producer dies holding a slot? Stale-slot reclamation, bounded-time recovery, and a defined behaviour under peer crash.

### 3.5 Fan-out — where the advantage becomes multiplicative
One producer, N consumers, all reading the same buffer with zero copies. Copy-based IPC pays N copies; zcring pays zero. The gap widens with N.

---

## 3a. Mapping the work to the rubric

Innovation, feasibility, scalability, impact — every work item below is mapped to one of these.

| Criterion | What carries it | Where it shows |
|---|---|---|
| **Innovation** | Adaptive spin-then-futex notification; true in-place construction (no copy *anywhere*, not one-copy rebranded); kernel arbitration layer for a problem userspace shared memory cannot solve | §3.3, §4 Layer 1, §4 Layer 3 |
| **Feasibility** | It runs, today, on commodity hardware; measured numbers rather than projections; acknowledged limitations; clean fallback layers | Week 1 exit criterion, §5 rigor, §8 |
| **Scalability** | Fan-out to N consumers where the advantage grows *linearly with N*; same framework spans IoT → embedded → desktop; multi-core behaviour under load | §3.5, consumer-count sweep in §5 |
| **Impact** | Deterministic tail latency is the blocker for real-time embedded workloads; camera → inference → display pipeline as a concrete deployable use case; robustness under peer crash | §3.1, §4 Layer 4, §7 demo |

Fan-out is the only work item that speaks directly to *scalability*, so it's
a core deliverable rather than a nice-to-have. The demo is built around a
concrete embedded use case (the camera pipeline) rather than around the
microbenchmark alone, for the same reason.

---

## 4. Architecture — layered so that scope cuts from the top

Each layer is independently demoable. If a layer fails or runs out of time, you present the layers beneath it and the submission still stands. **Nothing below depends on anything above it.**

```
Layer 4  (optional)  dma-buf / char-device arbitration, hardware buffer sharing
Layer 3  (stretch)   kernel module: access control + doorbell notification
Layer 2  (core)      adaptive notification, fan-out, crash recovery, epoll bridge
Layer 1  (must)      lock-free ring over memfd, true in-place construction
```

### Layer 1 — core transport
- `memfd_create` + `mmap` shared mapping; no kernel code required.
- Control block, cache-line aligned; head/tail padded to separate cache lines to avoid false sharing.
- Slot descriptor array + data arena.
- API is **reserve/commit** on the producer and **acquire/release** on the consumer, handing back raw pointers *into shared memory*. The user constructs the message in place. There is no `memcpy` anywhere in the path — this is what makes it genuinely zero-copy rather than one-copy-with-a-nice-name.
- Acquire/release memory ordering on the fast path; no `seq_cst`.

**Proof obligation:** show via `perf stat` that `memcpy` does not appear in the profile, and that latency is flat with respect to message size. Claiming zero-copy is cheap; proving it is the credibility.

### Layer 2 — notification, fan-out, robustness
Adaptive spin-then-futex as described in §3.3; `eventfd` bridge so the framework composes with `epoll`; multi-consumer fan-out with per-consumer cursors; stale-slot reclamation on peer death.

### Layer 3 — kernel component (only if Layers 1–2 are already winning)
A character device that hands out `memfd`/`dma-buf` handles, enforces access control between processes, and provides a doorbell. The honest justification for kernel code here is *arbitration*: pure userspace shared memory cannot stop a buggy or malicious peer from corrupting the ring. That is a defensible reason to be in the kernel, and it is the answer to "why does this need to be a kernel project?"

### Layer 4 — embedded credibility
`dma-buf` integration sketch for a camera → inference → display pipeline with no copies. Even a partial implementation reads as serious embedded Linux work.

---

## 5. Benchmark matrix

**Comparators:** `pipe`, UNIX socket (`SOCK_STREAM` and `SOCK_SEQPACKET`), POSIX `mqueue`, System V message queue, ZeroMQ, and — if time permits — **iceoryx** (Eclipse/Bosch), the real-world state of the art in zero-copy IPC. Benchmarking against iceoryx rather than only against pipes signals that you know the actual field.

**Metrics:** one-way latency, round-trip latency, throughput, p50/p99/p99.9/p99.99, jitter, CPU cycles per message, cache misses.

**Sweeps:** payload 64 B → 4 MB; consumer count 1 → 8; idle vs. `stress-ng` background load.

**Rigor:** multiple runs with confidence intervals, not single-shot numbers. Pin threads, use `isolcpus` / `nohz_full` where possible.

> **Measurement caveat — address this head-on.** Tail-latency numbers taken inside a VM are polluted by hypervisor scheduling jitter. Prefer bare-metal Linux for the determinism runs; if that is impossible, state the limitation explicitly and present relative comparisons rather than absolute guarantees. Judges respect an acknowledged limitation far more than a number they can tell is unsound.

---

## 6. Week-by-week

### Week 1 — 1–7 Aug — *core + first real numbers*
Register immediately. Build Layer 1 and a minimal benchmark harness against `pipe`. **Exit criterion: a real latency graph exists by 7 Aug.** This de-risks the abstract and converts it from a promise into a report.

### 8–10 Aug — *abstract*
Write and submit, using Week 1's measured data. Promise only what Layers 1–2 will demonstrably deliver. Because the prototype demo follows the abstract by roughly one week, there is no room to over-promise — judges will compare the two directly.

### Week 2 — 11–17 Aug — *depth*
Layer 2 in full: adaptive notification, fan-out, crash recovery, `epoll` bridge.

### Week 3 — 18–24 Aug — *rigor, and Layer 3 if earned*
Full benchmark matrix with statistical rigor, tail-latency and jitter work, iceoryx/ZeroMQ comparison. Attempt Layer 3 **only** if Layers 1–2 are complete and stable. **Code freeze 25 Aug.**

### Week 4 — 25–31 Aug — *demo and presentation*
No new features. Build the live demo, record the backup video, build slides, rehearse against hostile questions.

### Week 5 — 1–7 Sep — *present*
Buffer, final rehearsal, contingency.

Reserving a full week for presentation is the discipline most teams lack — they code until the night before and demo something broken. **Week 4 is not padding. Do not spend it coding.**

---

## 7. The demo

A live visual pipeline: synthesized video frames or sensor data flowing producer → N consumers, with a real-time dashboard showing latency histogram, throughput, and a copies-avoided counter — running the *identical* pipeline side by side over legacy IPC and over the framework. Toggle between them live and watch the histogram collapse.

**Record a backup video.** Live demos fail over video calls, and an online presentation to a judging panel is exactly the setting where a hang costs the competition.

---

## 8. Risk register

| Risk | Mitigation |
|---|---|
| Memory-ordering bug in the lock-free ring | Stay in userspace where bugs are printf-debuggable, not panic-debuggable. Run under TSan and stress tests early, not late. |
| Kernel module eats a week | Layer 3 is optional by construction and gated on Layers 1–2 being done. Never on the critical path. |
| VM jitter invalidates determinism claims | Prefer bare metal; otherwise state the limitation and use relative comparisons. |
| Scope creep | Cuts come from the top layer down, never from the middle. |
| Live demo fails on the call | Pre-recorded backup video. |
| Solo illness / lost days | Week 5 is deliberate slack. Do not spend it in advance. |

---

## 9. Standing principles

1. **Cut from the top, never the middle.** A complete Layer 1–2 beats a broken Layer 1–4.
2. **A measured number beats a claimed one**, everywhere — abstract, slides, answers.
3. **The presentation is the product.** Code that cannot be shown in ten minutes did not happen.
4. **State limitations plainly, in the same place as the claim.** A submission that does not know its own limits is wrong about itself, whoever reads it.
5. **Score against the rubric, not against your own taste.** Innovation, feasibility, scalability, impact. Work that advances none of the four is work that does not count, however satisfying it is to build.

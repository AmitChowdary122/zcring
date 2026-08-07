# Abstract — submission draft

Track 1 (Core) · *Zero-Copy Shared-Memory IPC Framework for Embedded Linux*

Every claim below is measured and traceable to committed raw data. Nothing is
projected. Nothing describes unbuilt work. This matters because the prototype
demonstration follows the abstract deadline by roughly one week — anything
promised here has to exist on screen in early September.

---

## Long form (~400 words)

**zcring: deterministic zero-copy IPC for embedded Linux**

Conventional Linux IPC copies every message twice through kernel buffers and
pays a system call per operation. For real-time embedded workloads — sensor to
control loop to actuator — that cost is large relative to the message itself,
and worse, it is *variable*. Determinism, not throughput, is usually what
constrains these systems.

zcring is a userspace IPC framework built on a lock-free multi-producer,
multi-consumer ring over a `memfd`-backed shared mapping. Its `reserve`/
`commit` and `acquire`/`release` operations hand back raw pointers directly
into shared memory, so applications construct and read messages *in place*.
There is no `memcpy` anywhere in the data path: the four memory passes a pipe
requires become two, and the system call disappears from message transfer
entirely.

Measured on bare-metal Ubuntu on a dual-core-plus-SMT Intel i3-1115G4 —
deliberately embedded-class hardware rather than a workstation — one-way p50
latency against pipes and UNIX sockets, with producer and consumer genuinely
writing and reading every payload byte:

| payload | zcring | best comparator | speedup |
|---|---|---|---|
| 64 B | 114 ns | 2.2 µs | **18–20×** |
| 1 KiB | 233 ns | 2.3 µs | ~10× |
| 4 KiB | 749 ns | 2.7 µs | 3.7× |

Ranges rather than point estimates because the entire suite was re-measured
on a separate occasion from a clean boot (see *Reproducibility* below), and
these are the bounds across both sessions.

**Scalability.** A broadcast mode delivers each message to N consumers with
per-consumer cursors and zero copies for any of them. Publication costs one
release store regardless of N, while copying transports pay N copies in and N
out — so the advantage *grows* with consumer count rather than staying a fixed
multiple. At large payloads this produces a genuine crossover, reproduced
independently at two sizes:

| payload | N=1 | N=2 | N=4 |
|---|---|---|---|
| 256 KiB | 0.67× | 1.15× | 1.61× |
| 1 MiB | 0.82× | 1.38× | **2.03×** |

zcring's absolute latency is nearly flat in N (187 µs → 203 µs at 1 MiB) while
the best comparator's grows roughly linearly (153 µs → 412 µs). That is the
copies-avoided argument appearing directly in measurement.

**Determinism.** Tail latency was traced to a concrete hardware source — deep
CPU idle-state exit latency, measured at 1048 µs on this part — and
eliminated. With deep C-states disabled, p99.9 falls to 1.04 µs with a
sub-2 µs spread across repetitions. The variance collapse matters more than
the mean: latency becomes predictable, which is what a real-time claim
requires.

**Correctness.** Exactly-once delivery is verified across 4 producers × 4
consumers over 200,000 messages and across process boundaries;
ThreadSanitizer runs clean. Three measurement artifacts found during
development — a harness that never touched the payload, SMT-sibling pinning,
and thermal throttling — are documented alongside the results rather than
omitted.

**Reproducibility.** Every benchmark is scripted and run at a stated,
sensitivity-tested offered rate, with all raw data committed. The complete
suite was then **re-measured on a separate occasion from a clean boot**, with
the machine re-quieted from scratch and the original results left untouched
for comparison. The scalability result reproduced within 1.5% at every point.
Single-consumer small-message ratios varied by ~7% between sessions, which is
why they are quoted as ranges rather than point estimates.

**Disclosed limitation.** Under the C-state-disabled configuration the
determinism claim requires, single-consumer transfers above 256 KiB are slower
than a UNIX socket (0.68–0.84×). Fan-out recovers this from N=2 onward. The
framework targets small, frequent, latency-critical messages; it is not a bulk
data mover at N=1.

---

## Short form (~150 words), if the field is tight

**zcring: deterministic zero-copy IPC for embedded Linux**

Conventional Linux IPC copies each message twice through the kernel and costs
a system call per operation — expensive for real-time embedded workloads, and
unpredictable, which matters more.

zcring is a lock-free multi-producer/multi-consumer ring over a `memfd`-backed
shared mapping. Applications construct and read messages in place through raw
pointers into shared memory: four memory passes become two, and the system call
leaves the data path entirely.

Measured on bare-metal embedded-class hardware (dual-core Intel i3-1115G4),
one-way p50 latency is **19.6× lower than a pipe at 64 B** and 10× at 1 KiB.
A broadcast mode delivers to N consumers with zero copies for any of them, so
the advantage grows with consumer count — at 1 MiB, 2.03× by N=4.

Deep CPU idle-state exit latency was identified as the dominant tail-latency
source and eliminated, bringing p99.9 to 1.04 µs with sub-2 µs spread.
Exactly-once delivery is verified under contention; ThreadSanitizer is clean;
all benchmark data is scripted and committed.

---

## Notes for whoever submits this

- **Say "dual-core with SMT", never "quad-core".** `nproc` reports 4; `lscpu`
  shows 2 cores per socket.
- **Lead with small-message latency.** Do not lead with a bandwidth multiple —
  see the disclosed limitation.
- **State the large-payload trade-off before a judge finds it.** Volunteering
  it reads as rigor; being caught on it costs the credibility of everything
  adjacent.
- Do not mention adaptive notification, the kernel arbitration layer, or
  dma-buf as claims. They are unbuilt. They belong in a "future work" slide at
  most, clearly labelled.

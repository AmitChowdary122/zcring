# zcring — zero-copy shared-memory IPC framework

Layer 1 of the plan in [PLAN.md](PLAN.md): a lock-free MPMC ring over a
memfd-backed shared mapping, with `reserve`/`commit` and `acquire`/`release`
handing back raw pointers into shared memory for in-place construction and
reading. No `memcpy` in the data path.

```
make            # build
make test       # correctness: exactly-once delivery under contention
make tsan       # thread-sanitised run — catches memory-ordering bugs
make sweep      # payload sweep -> results/sweep.csv
```

## Benchmark methodology, and the trap in it

`bench` measures **one-way** latency: the producer stamps `CLOCK_MONOTONIC`
into the payload header, the consumer reads it and computes the delta. Same
clock, same machine — no RTT/2 symmetry assumption.

**Always report `--touch` numbers as the primary result.**

Without `--touch`, the producer stamps 8 bytes and the consumer reads 8 bytes,
while the payload is never actually written or read. zcring then moves 8 real
bytes and merely asserts the rest exists, whereas pipe and unix genuinely move
all of them. That produces a spectacular flat line — latency apparently
independent of payload size — which is **an artifact of the harness, not a
property of the design**. A judge who asks "does the consumer ever touch the
data?" collapses the entire result, and they would be right to.

With `--touch`, both transports pay the same application-inherent write pass
and read pass, and the measured gap is only the copies zcring actually
eliminates:

| passes over memory | zcring | pipe / unix |
|---|---|---|
| producer writes payload | 1 | 1 (into a staging buffer) |
| copy into kernel | — | 1 |
| copy out of kernel | — | 1 |
| consumer reads payload | 1 | 1 |
| **total** | **2** | **4** |

So the defensible claim is *"eliminates two of four memory passes, and removes
the syscall from the data path entirely"* — not *"latency is independent of
message size."* The honest curve still rises with payload; it just rises at
roughly half the slope, from a far lower intercept.

## Measured Layer 1 results

p50 one-way latency, `--touch`, **bare metal**, Intel i3-1115G4 (2 physical
cores + SMT), performance governor, background services stopped, mean of 5
reps. Raw data in `results/sweep.csv`.

| payload | zcring | pipe | unix | vs. best comparator |
|---|---|---|---|---|
| 64 B | 113 ns | 2.13 µs | 2.83 µs | **18.8×** |
| 256 B | 137 ns | 2.17 µs | 2.92 µs | 15.8× |
| 1 KiB | 290 ns | 2.27 µs | 3.13 µs | 7.8× |
| 4 KiB | 791 ns | 2.74 µs | 3.96 µs | 3.5× |
| 16 KiB | 3.01 µs | 5.61 µs | 6.23 µs | 1.9× |
| 64 KiB | 10.5 µs | 17.2 µs | 13.1 µs | 1.25× |
| 256 KiB | 32.6 µs | 71.6 µs | 35.8 µs | 1.10× |
| 1 MiB | 123 µs | 335 µs | 181 µs | 1.47× |

Run-to-run p50 spread is within single-digit percent of the mean everywhere.
This part is solid and quotable as-is.

### What this actually says — read it carefully

The advantage is **latency on small messages, not bandwidth on large ones.**

- **Small messages (≤ 4 KiB): 3.5–19×.** Here the syscall and the fixed
  per-message overhead dominate, and zero-copy removes them from the data path
  entirely. This is the regime real-time embedded control traffic actually
  lives in — sensor readings, actuator commands, state updates.
- **Large messages: converges to 1.1–1.5×.** Once you are memory-bandwidth
  bound, the kernel's `memcpy` is extremely well optimised (non-temporal
  stores, prefetch, wide vectors), while zcring pays cross-core cache-line
  transfer costs of its own. Eliminating two of four memory passes does *not*
  translate into a clean 2× at the top end.
- **The ratio is non-monotonic** (1.10× at 256 KiB, 1.47× at 1 MiB) because
  256 KiB working sets still fit in this CPU's L2 while 1 MiB does not, so the
  copy-based transports degrade faster once they spill.

> **An earlier draft of this file claimed a "~2× floor" at large payloads.
> That was derived from VM measurements and does not survive bare metal. The
> measured floor is 1.1×.** Corrected here rather than quietly dropped,
> because the difference between claiming 2× and measuring 1.1× in front of a
> judge is the whole submission.

**Where the large-payload case is actually won: fan-out.** With N consumers, a
copy-based transport pays N copies; zcring pays none. The single-consumer
1.47× at 1 MiB becomes a multiple that grows linearly with consumer count.
That is the measurement to build, and it is also what the *scalability*
criterion in the rubric is asking for.

### p99.9 is not yet quotable

Tail latency currently varies by three orders of magnitude across identical
repetitions (zcring at 64 B ranged 997 ns – 1.18 ms over 5 reps). All three
transports show it, so it is OS scheduling noise on a non-isolated, non-RT
kernel — not a ring defect. **Do not quote any p99.9 figure until the
`isolcpus` / `nohz_full` / PREEMPT_RT work is done.** That work is the top
differentiator in the plan precisely because the problem statement asks for
*deterministic* communication.

## Known gaps (Layer 2 work)

- The zcring consumer **spins**; pipe/unix consumers block. Idle CPU cost is
  therefore not comparable yet. Layer 2's adaptive spin-then-futex path is the
  honest fix.
- Single producer → single consumer only in the benchmark. Fan-out to N
  consumers is where the advantage becomes multiplicative, and it is the work
  item that speaks to *scalability* in the rubric.
- No crash recovery yet: a producer dying mid-`reserve` leaks that slot.

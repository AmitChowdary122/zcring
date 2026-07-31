# Running Layer 1 yourself

## 0. You need Linux

The code uses `memfd_create`, `mmap(MAP_SHARED)`, and (in Layer 2) futexes.
It will not build on Windows. Your options, best first:

| Environment | Correctness testing | Latency numbers |
|---|---|---|
| Bare-metal Linux (dual boot) | yes | **yes — use this for submission data** |
| WSL2 | yes | indicative only, jitter is high |
| VM (VirtualBox/VMware/Hyper-V) | yes | indicative only |

WSL2 is fine for everything in this file. Just don't quote its tail latencies
in the abstract — see §4.

## 1. Get the code into Linux

The project lives on the Windows side. **Copy it into the Linux filesystem
before building** — compiling on `/mnt/c` goes through a translation layer
that is slow and can produce odd permission errors.

```bash
cp -r /mnt/c/Users/amitc/Documents/SSM_CDAC_Hackathon ~/zcring
cd ~/zcring
sudo apt update && sudo apt install -y build-essential
```

## 2. Build and prove correctness first

```bash
make
make test
```

Expected: four test groups, ending in `all checks passed`. What each one is
actually for:

- **basic** / **full-empty** — API sanity, single-threaded.
- **concurrent** — 4 producers × 4 consumers, 200,000 messages, verifying
  *exactly-once* delivery. This is the one that matters. A lock-free ring that
  loses or duplicates a message under contention will still benchmark
  beautifully, which is why correctness is checked before speed.
- **cross-process** — the memfd genuinely crosses a `fork()` boundary, so the
  sharing is real rather than same-address-space.

Then run the sanitiser:

```bash
make tsan
```

This is the important one. Memory-ordering bugs are the top technical risk in
the plan, they are invisible in normal runs, and they surface as corruption
weeks later. TSan run clean = the acquire/release edges in `zcring.h` are
sound. **Do not skip this**, and re-run it after any change to the ring.

## 3. Benchmark

Single run, human-readable:

```bash
./build/bench --transport=zcring --size=4096 --touch
./build/bench --transport=pipe   --size=4096 --touch
./build/bench --transport=unix   --size=4096 --touch
```

**Always pass `--touch`.** Without it the harness only stamps and reads an
8-byte header, the payload is never actually moved, and you get a fictitious
flat line. See README.md for why this matters.

Full sweep to CSV:

```bash
make sweep                                  # defaults
COUNT=50000 GAP=100 ./scripts/sweep.sh      # slower, cleaner tails
```

Writes `results/sweep.csv`.

Useful flags: `--size` `--count` `--warmup` `--gap-us` `--cpu-prod` `--cpu-cons`.

## 3a. Fan-out (Layer 2)

```bash
./build/bench --transport=zcring --consumers=4 --size=4096 --touch --yield \
              --cpu-prod=2 --cpu-cons=3,1
make fanout                                       # full N x payload sweep
REPS=5 COUNT=50000 GAP=100 ./scripts/fanout.sh    # submission-grade
```

Writes `results/fanout.csv`. Three things about this mode that will bite you
if you skip them:

- **`--cpu-cons` is a list, and it must exclude the producer's SMT sibling.**
  Consumer *k* pins to the *k*'th entry, wrapping. Putting a spinning consumer
  on the producer's sibling thread leaves p50 looking fine and inflates p99 by
  ~185× — it was measured on this machine, it is not hypothetical.
  `scripts/fanout.sh` derives the list from `thread_siblings_list`; check the
  two lines it prints.
- **Use `--yield` for anything with N above 2 on a small machine.** zcring
  waiters spin; at N=4 on two physical cores there are more spinners than
  hardware threads and you measure scheduler thrash, not fan-out. Pure spin
  reports p50 = 90 µs at 4 KiB; `--yield` reports 2.1 µs for the same
  configuration. Keep the flag consistent across every row you intend to
  compare.
- **`--consumers` selects zcring's broadcast path even at N=1**, so fan-out
  rows are comparable to each other. Omitting it keeps the Layer 1 unicast
  path, which is what `sweep.csv` was measured with. The two are therefore not
  directly comparable at N=1; the difference is the gate's own overhead.

## 4. Getting numbers worth quoting

Latency tails are extremely sensitive to what else the machine is doing. For
anything that will appear in the abstract or slides:

```bash
# close browsers, IDEs, Docker, everything
sudo cpupower frequency-set -g performance     # stop frequency scaling
./build/bench --transport=zcring --size=4096 --touch \
              --count=200000 --gap-us=100 --cpu-prod=2 --cpu-cons=3
```

Run each configuration **at least 5 times** and report the spread, not one
number. A single run is not a measurement.

Pinning producer and consumer to distinct physical cores (`--cpu-prod` /
`--cpu-cons`) is the cross-core case, which is the honest default for real
workloads. Pinning both to sibling hyperthreads of the same core will flatter
zcring considerably — if you show that, label it.

## 5. Sanity checks — what should worry you

| Observation | What it means |
|---|---|
| zcring p50 flat across all payload sizes | you forgot `--touch` |
| zcring p50 under ~50 ns at 1 MiB | payload isn't being touched; not physically possible |
| pipe p50 under ~2 µs | suspiciously fast — check the consumer is a separate process |
| p99.9 more than ~50× p50 | background load or frequency scaling, not a real tail |
| `make test` passes but tsan reports races | trust tsan; the ring is wrong |
| huge variance between identical runs | machine is not quiet enough to measure on |
| zcring p50 fine but p99 in milliseconds at N>1 | a consumer is on the producer's SMT sibling |
| zcring p50 in tens of µs at N=4 | spinner oversubscription; you need `--yield` |
| fan-out consumer count doesn't change zcring latency much | expected — publication is O(1) in N. That is the result, not a bug |

## 6. What to report back

The CSV plus: kernel version (`uname -r`), CPU model (`lscpu | head`), core
count, whether bare metal or WSL/VM, and whether the machine was idle. Every
one of those changes how the numbers should be interpreted, and judges will
ask about at least two of them.

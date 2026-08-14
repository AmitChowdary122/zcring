---
status: decided
confidence: high
verified: 2026-08-09
---

# The measurement machine — say "dual-core with SMT"

> **The measurement laptop died on 14 Aug 2026.** Its SSD now dual-boots in
> the Ryzen machine. Every committed number came from hardware that no longer
> exists — valid as measured, not reproducible by us on demand. See
> `results/PROVENANCE.md` and the guard in `scripts/lib.sh:check_machine()`,
> which refuses to write a canonical dataset filename on a non-i3 CPU.

| Machine | Role |
|---|---|
| Intel **i3-1115G4**, 2 physical cores + SMT | **All quoted measurements. DEAD as of 14 Aug.** |
| **Ryzen 9 270**, 8C/16T — now hosts the Ubuntu SSD as a dual boot, plus WSL2 (Kali) on Windows | The only machine. Development, builds, Layer 3 VM work. |

## Never say "quad-core"

`nproc` reports 4. `lscpu` shows **2 cores/socket**. Describe it as
**"dual-core with SMT"**. Getting this wrong in front of a kernel-hackathon
judge is a credibility loss out of all proportion to the error.

## WSL2 numbers are never quotable

Hypervisor jitter inflates even p50 by ~8–10× on the comparators and p99.9 by
orders of magnitude. `cpupower`, `isolcpus`, `nohz_full` and PREEMPT_RT are
all meaningless there. WSL2 is fine for `make test` and `make tsan`; nothing
else.

## The claims stay anchored to the i3

A dead laptop does not change which platform the claims should be *about*.
2–4 cores is what embedded deployment looks like; an 8-core desktop part is
less representative of the problem statement, not more — the same reasoning as
[[decisions/no-ryzen-n8-run]], which was decided before the laptop failed.

If the Ryzen is ever measured it is a **secondary, labelled** dataset, and the
interesting question is not "are the numbers better" but **"do the conclusions
survive a different microarchitecture?"** Zen vs Tiger Lake, 8 cores vs 2.
Structural agreement across two unlike parts is stronger evidence than a third
run on the same box would have been. `OUT_SUFFIX=_ryzen` is mandatory and
enforced.

## Two physical cores is a feature

The problem statement is *embedded* Linux, where 2–4 cores is typical. Frame
it as "measured on a platform representative of embedded deployment." This is
not a concession — it is the reason [[decisions/no-ryzen-n8-run]] went the way
it did.

## Benchmarking discipline

- Quiet the machine — browsers, editors, Docker all closed.
- `sudo cpupower frequency-set -g performance`
- `sudo cpupower idle-set -D 0` — [[hypotheses/cstate-tail]]
- Pin to distinct **physical** cores — [[traps/smt-siblings]]
- `REPS=5` minimum for anything quoted. One run is not a measurement.
- Commit raw CSVs. Judges may ask.

Only `/usr/bin/cpupower` is NOPASSWD in sudoers. Anything needing a root
`sysctl` (e.g. `vm.nr_hugepages`) has to be run by hand — see
[[hypotheses/tlb-hugepages]].

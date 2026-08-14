---
status: confirmed
confidence: high
source: STATUS.md "Huge-page backing"; direct VMA probe (THPeligible, ShmemPmdMapped); /sys/kernel/mm/transparent_hugepage/shmem_enabled
verified: 2026-08-08
---

# CONFIRMED NEGATIVE — `thp-advise` is not huge pages on this box

**Do not read `pages=thp-advise` as huge pages.** It would have produced a
fake positive in [[hypotheses/tlb-hugepages]].

## Why

Shared-memory THP is gated on
`/sys/kernel/mm/transparent_hugepage/shmem_enabled`, which is **`never`** on
[[claims/machine]].

So: the `madvise` returns 0, `VM_HUGEPAGE` gets set, and the arena is **4 KiB
pages regardless**. Probed directly, the arena VMA reports `THPeligible: 0`
and `ShmemPmdMapped: 0` — with both the virtual address and `arena_off`
correctly 2 MiB aligned.

**The alignment machinery is right; the kernel policy declines.**

## Why the string is named the way it is

`thp-advise`, not `thp` — **a request, not a receipt.** Only `pages=hugetlb`
is a guarantee.

This naming choice is doing real work: a column reading `thp` would have
silently invited the conclusion that the huge-page arm was testing huge pages.
Worth reusing the pattern — **name a status field after what was asked for
versus what was obtained, whenever the two can differ.**

## Consequence

Real 2 MiB pages need a hugetlbfs pool, which needs `vm.nr_hugepages` set by
root. Only `/usr/bin/cpupower` is NOPASSWD on that machine, so the pool has to
be reserved by hand — the user runs it, I cannot.

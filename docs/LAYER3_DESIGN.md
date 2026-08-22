# Layer 3 — kernel-enforced arbitration (DESIGN ONLY — NOT IMPLEMENTED)

> **Status: specified, not built. No kernel code exists.** This document is a
> design brief, included because it states the architectural argument for why
> a kernel component is warranted at all — which is the question a
> userspace-only zero-copy framework has to answer. Nothing in it is claimed
> as working software. Written 9 Aug 2026; if it is ever implemented, it
> should be built and tested inside a VM (see §8).

---

## 1. Why this layer exists

The hardest question this submission will be asked is:

> **"How is this not iceoryx?"**

The answer has to be structural, not a benchmark. It is this:

**iceoryx is pure userspace.** Every participant maps the shared segment
`PROT_READ | PROT_WRITE`, because the ring's own bookkeeping requires it.
A buggy or malicious consumer can therefore scribble anywhere in the segment
and corrupt every other peer — and no amount of userspace design can prevent
that, because the process already holds a writable mapping. A broker daemon
can decide *who may attach*; it cannot revoke a page the MMU has already been
told is writable.

**Only the kernel can hand out a mapping that is physically unable to write.**

That is the gap Layer 3 closes, and it is why this scores on **Novelty &
Innovation** *and* **Security** — the two criteria in `dev/HACKATHON.md` where
this project is currently weakest.

`zcring` today has exactly the iceoryx weakness. Say that plainly in the
writeup; the honesty is worth more than the pretence, and it sets up the fix.

---

## 2. The constraint that must not be violated

> **The kernel goes in the setup path, never in the data path.**

The project's central claim is that it removes the syscall from the data
path ([[claims/memory-passes]] in the vault; `README.md` for the numbers). A
Layer 3 that validates each message would put a syscall back and destroy the
thing being submitted.

So the division is:

| Path | Who | Cost |
|---|---|---|
| create / attach / set permissions / evict | **kernel**, via ioctl | once per peer, per ring |
| reserve / commit / acquire / release | **userspace**, unchanged | zero syscalls |

The kernel arbitrates **who may map what, with which protections**. Once the
mapping exists, the fast path is byte-for-byte the code that is already
measured and TSan-clean. **No number in `results/` should change.** If a
sweep after Layer 3 differs from before, something is wrong — that is the
regression test.

---

## 3. Threat model

Write this table into the module header and the submission. Being explicit
about what is *not* defended is what makes the defended parts credible.

| Threat | Status | Mechanism |
|---|---|---|
| Consumer corrupts message payloads | **closed, Phase 1** | arena mapped `PROT_READ` for consumers; a write traps in hardware |
| Consumer corrupts ring bookkeeping (head/tail/seq) | **closed, Phase 2** | control block split; consumer gets RW only on its own cursor page |
| Unauthorised process attaches | **closed, Phase 3** | UID/GID check at `ATTACH`; device node permissions |
| Consumer dies holding a slot | **closed, Phase 3** | kernel `release()` is authoritative; unlike userspace reaping it cannot be skipped |
| Consumer stalls the ring by never advancing | **mitigated** | kernel-side deadline eviction; a *policy* choice, so document the trade-off against backpressure |
| Producer writes garbage | **out of scope** | the producer is the data source; if it is hostile the data is already worthless |
| Side-channel inference from timing | **out of scope** | state it; do not hand-wave it |
| Physical/DMA attack | **out of scope** | Layer 4 territory |

**The honest finding to lead with** (see §6): the write-protection property
comes from the **broadcast topology**, not from shared memory as such. That
is a more interesting claim than "we added a kernel module".

---

## 4. Architecture

A misc character device, `/dev/zcring`.

```
ZCRING_IOC_CREATE   -> ring fd. Caller becomes owner + sole producer.
ZCRING_IOC_ATTACH   -> ring fd, consumer role. Kernel assigns a cursor slot.
ZCRING_IOC_EVICT    -> owner-only; forcibly removes a consumer.
ZCRING_IOC_INFO     -> layout offsets, ABI version, role, cursor index.
mmap(ring_fd)       -> kernel applies per-role protections. This is the layer.
```

`mmap` is where the whole design lives. In `zcring_mmap()`, inspect the
calling file's role and either allow or strip `VM_WRITE`:

- **producer** — control block RW, arena RW
- **consumer** — control block RO, arena **RO**, own cursor page RW

Clear `VM_MAYWRITE` as well as `VM_WRITE`, or the consumer can simply
`mprotect()` the write permission back. This is the single easiest way to ship
a security layer that does nothing.

### Memory layout change

Phase 2 needs each consumer's cursor on its **own page**, so it can be mapped
RW individually while everything around it stays RO:

```
page 0        ctrl: magic, version, mode, head, tail, gate_cache, futex_word
page 1..N     one page per consumer cursor          <- N x 4 KiB of padding
page N+1..    slot seq array
arena         slot_count * slot_size
```

That costs `ZC_MAX_CONSUMERS * PAGE_SIZE` of address space — 64 KiB at N=16,
irrelevant next to the arena — and it **bumps the ABI to v3**. Update the
static layout asserts, re-run `make test` and `make tsan`.

Do **not** do this in Phase 1. Phase 1 protects the arena only, keeps the
current layout, and stays at ABI v2.

---

## 5. Phasing, and the decision gate

Cut from the top *within* Layer 3, exactly as at project level.

### Phase 1 — arena write protection (target: 2 days)

Misc device; `CREATE`/`ATTACH`; `mmap` with the arena RO for consumers.
No layout change, no ABI bump.

**This alone is a complete, demonstrable security property.** A consumer
cannot corrupt another consumer's data. Everything after it is refinement.

Done when: a consumer's write to the arena takes `SIGSEGV`, and
`make test` / `make tsan` still pass on the userspace-only path.

### Phase 2 — control-block protection (target: 1 day)

Page-per-cursor layout, ABI v3, control block RO except the consumer's own
cursor page.

### Phase 3 — access control and lifecycle (target: 1 day)

UID/GID check at attach; `EVICT`; dead-peer cleanup in `release()`.

### Phase 4 — the demo (target: 1 day)

See §7. **Do not skip this.** An unshown kernel module scores nothing.

### The gate — read this before starting

> **If Phase 1 is not working by end of day 3, stop and ship Layers 1–2.**

A half-built kernel module is *worse than none*: it undercuts Implementation
Quality, invites questions a clean Layers 1–2 submission never faces, and the
Stage 1 form still has to be written. Layers 1–2 are already a complete,
measured, reproducible submission. **Layer 3 is upside, not rescue.**

Non-negotiable throughout:

- **The userspace-only path keeps working with no module loaded.** Layer 3 is
  optional hardening. `zc_create()` must behave exactly as today when
  `/dev/zcring` is absent. This is both a feasibility requirement and what
  makes the demo safe to run.
- **No change to the fast path.** Re-run a sweep at the end and diff against
  `results/sweep.csv`.

---

## 6. The insight worth writing up

Full write-protection works in **broadcast mode** and *cannot* work in the
unicast MPMC mode.

In broadcast, a consumer only ever advances its own cursor — it never mutates
shared ring state, so it needs no write access to anything shared. In unicast
MPMC, consumers must write slot sequence numbers to release slots, so they
need write access to the slot array, and a malicious consumer can corrupt it.

**So the security property comes from the topology, not from shared memory.**
Fan-out is not merely a scalability feature that happens to also be safe;
one-writer-many-readers is what *makes* kernel-enforced isolation possible.

Two things follow, and both are worth saying:

1. It ties Layer 3 to the fan-out work rather than bolting it on.
2. It is a real limitation, stated before anyone finds it — unicast mode is
   *not* hardened, and cannot be by this mechanism.

This paragraph is probably worth more to the Novelty score than the code is.

---

## 7. The demo

Two terminals, ninety seconds, no narration required:

```
$ ./demo/malicious_consumer --no-kernel      # today's userspace-only zcring
  attached. writing 0xDEADBEEF across the arena...
  [producer] CORRUPTION: slot 41 checksum mismatch
  [consumer 2] CORRUPTION: slot 41 checksum mismatch

$ ./demo/malicious_consumer                  # same binary, /dev/zcring present
  attached. writing 0xDEADBEEF across the arena...
  Segmentation fault (core dumped)
  [producer] 200000 messages, 0 errors
  [consumer 2] 200000 messages, 0 errors
```

**Same binary, same attack, one flag.** That contrast is the entire argument
for the layer, and it answers the iceoryx question without a slide.

Add `demo/malicious_consumer.c` to the repo. A judge reading the source and
seeing an honest attack — not a strawman — is worth more than the module
listing.

---

## 8. Practical notes for the build machine

- **Everything Layer 3 happens in a VM. The module is never loaded on the
  measurement box.** Decided 9 Aug, after checking prerequisites: that machine
  has **Secure Boot enabled** (kernel `7.0.0-28-generic`, headers present), so
  an unsigned module would need Secure Boot disabled or a MOK enrolled — and
  it holds the only quotable dataset, where a module bug is a panic rather
  than a failed test.

  This costs nothing, because **no part of Layer 3 needs bare metal**:

  - The security demo is *functional*, not a latency measurement. Hypervisor
    jitter has no bearing on whether a write traps.
  - "The fast path is unchanged" needs no module on bare metal either. With no
    module loaded, the code path **is** today's code path, already measured.
    For corroboration run a module-loaded vs not A/B *inside the VM*: absolute
    numbers there are not quotable, but both arms carry identical jitter, so
    the difference between them is a valid controlled comparison.
  - Secure Boot is off by default under QEMU/KVM unless OVMF secure boot is
    deliberately enabled.

  If a bare-metal fast-path check is ever wanted, it is a plain sweep with no
  module loaded — which is exactly what `results/sweep.csv` already is.
- `sudo apt install linux-headers-$(uname -r) build-essential` — out-of-tree
  module build, separate `Makefile` under `kernel/`.
- `dmesg -w` in a spare terminal, always.
- The module is GPL-licensed by necessity (`MODULE_LICENSE("GPL")`); note the
  licensing boundary against the userspace library in the repo.
- Commit early and often. A panic loses the working tree, not just the shell.

## 9. Model

**Opus 5** for the module and the `mmap` protection logic — memory-management
and lifetime bugs here are kernel panics rather than test failures, and the
`VM_MAYWRITE` class of mistake is silent.

Sonnet 5 is fine for the demo program and the Makefile.

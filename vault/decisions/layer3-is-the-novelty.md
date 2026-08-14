---
status: decided
confidence: high
source: docs/LAYER3_DESIGN.md
verified: 2026-08-09
---

# Layer 3 is the novelty claim, not extra credit

## The hardest question I will be asked

**"How is this not iceoryx?"**

iceoryx is pure userspace. Every participant maps the segment RW because the
ring's bookkeeping requires it, so a buggy or malicious consumer can scribble
anywhere and corrupt every peer. A broker daemon can decide *who attaches*; it
cannot revoke a page the MMU has already been told is writable.

**Only the kernel can hand out a mapping that is physically unable to write.**

`zcring` today has exactly the iceoryx weakness. **Say that plainly** — the
honesty is worth more than the pretence and it sets up the fix.

## Why it scores twice

`HACKATHON.md`'s rubric scores **Security** as its own criterion. Layer 3
scores on **Novelty & Innovation** *and* **Security** — the two criteria this
project is weakest on. Nothing else outstanding has that leverage.

## The constraint that must not be violated

**The kernel goes in the setup path, never the data path.** A Layer 3 that
validated each message would put the syscall back and destroy the submission's
central claim. Kernel arbitrates create/attach/permissions/evict; reserve,
commit, acquire and release stay untouched.

**Regression test: no number in `results/` may change.** Re-run a sweep after
and diff against `results/sweep.csv`.

## The insight worth more than the code

Full write-protection works in **broadcast mode and cannot work in unicast
MPMC**. In broadcast a consumer only advances its own cursor and never mutates
shared state, so it needs no write access to anything shared. In unicast,
consumers must write slot sequence numbers to release slots.

**The security property comes from the topology, not from shared memory.**
One-writer-many-readers is what *makes* kernel-enforced isolation possible —
which ties Layer 3 to [[claims/fanout-crossover]] rather than bolting it on,
and states the unicast limitation before anyone finds it.

## Scope and gate

Phased in `docs/LAYER3_DESIGN.md`: arena RO (2d) → control-block split, ABI v3
(1d) → access control and lifecycle (1d) → demo (1d).

> **If Phase 1 is not working by end of day 3, stop and ship Layers 1–2.**

A half-built kernel module is worse than none — it undercuts Implementation
Quality and invites questions a clean Layers 1–2 submission never faces, and
[[submission/stage1-checklist]] still has unwritten form text. **Layer 3 is
upside, not rescue.** [[decisions/cut-from-the-top]] is not suspended for it.

The userspace-only path must keep working with no module loaded.

## Status

**Design written 9 Aug (`docs/LAYER3_DESIGN.md`). No code yet.** Build on
Ubuntu, develop in a VM first — a module bug is a panic and the measurement
box holds the only quotable dataset. *Opus 5* for the module; the
`VM_MAYWRITE` class of mistake is silent.

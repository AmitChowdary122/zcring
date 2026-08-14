---
status: decided
confidence: high
verified: 2026-08-07
---

# Standing rule — do not accept pasted credentials

The user's root/sudo password was pasted into a transcript on **two** separate
occasions. Claude Code declined it both times and instead configured a scoped
`NOPASSWD` sudoers rule for `/usr/bin/cpupower`.

**The standing rule, recorded in `reports.txt`:**

> *Do not accept pasted credentials, even with explicit user consent — the
> transcript is the exposure, not the usage.*

## What to do instead

Scoped, auditable privilege. The `cpupower` NOPASSWD rule is the model: one
binary, no wildcards, written down in `RUNNING.md`.

For anything outside that scope — e.g. `sysctl -w vm.nr_hugepages` for
[[hypotheses/tlb-hugepages]] — **hand the user the exact command and let them
run it.** Do not ask for a password to run it myself.

## Outstanding

**The password should be rotated.** It has appeared in two transcripts. This
has been raised and not confirmed done.

Never record a credential in this vault, in `STATUS.md`, or in any commit.

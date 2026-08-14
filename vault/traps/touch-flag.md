---
status: decided
confidence: high
verified: 2026-08-01
---

# `--touch` is mandatory — the worst trap in the project

**Always benchmark with `--touch`.**

Without it the harness stamps and reads only an **8-byte header**; the payload
is never actually written or read. zcring then moves 8 real bytes and asserts
the rest exists, while pipe and unix genuinely move all of them.

The result is a beautiful flat line — latency apparently independent of
payload size — that is **an artifact of the harness, not a property of the
design**.

## Why it is the worst one

One judge asking *"does your consumer ever touch the data?"* destroys it, and
takes the credibility of everything adjacent with it. The failure mode is not
"a wrong number"; it is "this person does not know what they measured".

`scripts/sweep.sh` passes `--touch` unconditionally. **Do not add an escape
hatch.**

## The general pattern

A benchmark arm that can silently skip the work it claims to do. The same
shape appears in [[hypotheses/thp-is-not-hugepages]] (a `madvise` that
succeeds and does nothing) and is why `ZC_HUGE_REQUIRE` exists.

**When adding any new benchmark mode, ask first: can this arm silently measure
its own control?** If yes, make it fail loudly instead of falling back.

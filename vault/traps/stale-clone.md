---
status: decided
confidence: high
verified: 2026-08-07
---

# Always lead a handed-over command with `cd`

Three command runs failed because they were pasted without a leading `cd` and
landed in a **stale `~/zcring` clone** on the Kali box that lacked the needed
commits. One of them produced an empty committed file.

## Rule

Every command I hand over starts with `cd ~/zcring &&` (or the Windows path).
No exceptions, even when the previous command obviously left them there — the
user may open a new terminal between messages.

If a run looks wrong in a way that suggests missing code, **suspect the
working directory before suspecting the code.**

Related: [[traps/env-var-spacing]] for the rest of the handover rules.

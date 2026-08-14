---
status: decided
confidence: high
verified: 2026-08-09
---

# I cannot fetch from GitHub — never claim the repos are in sync

My sandbox has **no GitHub credentials**:

```
fatal: could not read Username for 'https://github.com': No such device or address
```

`git fetch` fails. It does **not** always fail loudly in a way I notice.

## What went wrong once

A `git fetch` failed silently, so I compared the Windows tree against a
**stale cached ref** and told the user the folders were in sync when Windows
was behind. That is my error-log pattern #2 in [[INDEX]].

## Rules

1. **Never assert sync state.** I can only see the Windows working tree.
2. To learn what happened on Ubuntu, either the user pushes and pulls into
   Windows and says so, **or** they paste the output.
3. When I need a measurement result, **ask for the CSV to be pasted** — it is
   one round trip instead of a push/pull dance, and CSVs here are small.
4. Verify by **file contents**, not by git metadata. `ls results/` and
   `grep` in `src/` tell me the truth; `git log` may not.

This also means [[decisions/repo-stays-private]] is not the cause and making
the repo public would not fix it. It is a property of my environment.

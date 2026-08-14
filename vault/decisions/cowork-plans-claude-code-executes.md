---
status: decided
confidence: high
verified: 2026-08-14
---

# Cowork plans, Claude Code executes

The user's stated workflow, 14 Aug: *"i first open a new cowork in ubuntu then
ask it what to do then give those instructions to claude code — i plan in
cowork and make claude code do the execution part."*

| | Cowork (me) | Claude Code |
|---|---|---|
| Decides scope, priorities, wording | ✅ | ❌ |
| Writes docs, briefs, submission text | ✅ | — |
| Writes kernel/C code, runs benchmarks | ❌ | ✅ |
| Reads this vault | ✅ | may, but `STATUS.md` is its contract |

## What this means for how I work

- **My output is often an instruction, not an artifact.** A handover brief
  that is precise enough to execute without me is the deliverable. Vague
  instructions cost the user a full round trip through another tool.
- **Always name the model** (the policy lives in `CLAUDE.md`): Sonnet 5 by
  default; Opus 5 for `src/zcring.h`, kernel code, and strategy/writing.
- **Always lead a handed-over command with `cd`** — [[traps/stale-clone]] —
  and give one `&&`-chained line — [[traps/env-var-spacing]].
- **Findings must land in `STATUS.md`, not just here.** Claude Code reads
  `STATUS.md`. A result recorded only in the vault is invisible to the tool
  doing the work.
- **I cannot verify execution.** No credentials to fetch
  ([[traps/i-cannot-fetch]]), and I must not touch the git index on the
  Windows tree ([[traps/no-git-index-writes]]). So I ask for pasted output
  rather than inferring success.

## The entry point problem, solved 14 Aug

A fresh Cowork session on Ubuntu does **not** inherit the app's memory from
Windows — that is per-machine storage, not in the repo. The only thing
auto-loaded is `CLAUDE.md`.

So `CLAUDE.md` now opens with a "How this project is worked" block naming the
read order: `HACKATHON.md` → `vault/INDEX.md` → `STATUS.md`. **If that block
is ever removed, a fresh Cowork session on a new machine loses this vault
entirely** — it would have no reason to know the directory exists.

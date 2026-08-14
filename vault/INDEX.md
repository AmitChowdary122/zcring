# zcring vault — index

**Read this file first, every session. Read nothing else until you know what
you need.** Every line below is a pointer; open only the notes the current
task actually touches.

> **This vault is committed** (changed 14 Aug — the Ubuntu SSD now dual-boots
> in the same laptop, so sessions move between machines). It must be
> **untracked before the repo goes public** at submission — see
> [[00-how-i-use-this]] for the command. Cross-machine facts still belong in
> `STATUS.md` as well: Claude Code reads that, not this.

## Where authority lives

| Question | Authoritative file | Not this |
|---|---|---|
| Rules, deadlines, form fields, rubric | `HACKATHON.md` | anything in this vault |
| Current build/measurement state | `STATUS.md` (committed) | this vault |
| Design rationale | `src/zcring.h` | this vault |
| Methodology, published numbers | `README.md` | this vault |
| What I already learned and must not relearn | **this vault** | — |

This vault is *my* working memory, not a second source of truth. When they
disagree, the repo wins and I fix the vault.

## Claims — what is safe to say out loud

- [[claims/64b-small-message]] — the headline. 18–20×, spin waiter only. **Quote as a range.**
- [[claims/large-payload-n1]] — we **lose** at 256 KiB–1 MiB, N=1. Disclose before a judge finds it.
- [[claims/large-payload-tail]] — worst case **bounded**: 0/20 zcring runs over 1 ms, 7/10 unix. **Do not quote p99.9 here.**
- [[claims/fanout-crossover]] — crossover at **N=2**. Do not claim growth past it.
- [[claims/determinism-p999]] — p99.9 quotable, **p99.99 is not**.
- [[claims/memory-passes]] — 2 passes vs 4, and why that isn't 2× at large sizes.
- [[claims/machine]] — "dual-core with SMT", never "quad-core". WSL2 numbers never quotable.

## Decisions — settled, don't relitigate

- [[decisions/problem-statement]] — Track 1 / zcring. **Locked by the rules.**
- [[decisions/cut-from-the-top]] — scope discipline.
- [[decisions/no-ryzen-n8-run]] — dropped deliberately, with reasoning.
- [[decisions/spin-is-the-sweep-default]] — why `WAITER=spin`.
- [[decisions/ai-criterion-answer]] — adaptive notification = "Inbuilt Model". Never say "AI".
- [[decisions/layer3-is-the-novelty]] — the answer to "how is this not iceoryx?"
- [[decisions/repo-stays-private]] — public only at submission.

## Hypotheses — especially the dead ones

- [[hypotheses/cstate-tail]] — **confirmed.** The single biggest determinism win.
- [[hypotheses/busy-spin-power]] — **refuted, twice.** Mine. Do not resurrect.
- [[hypotheses/offered-rate-confound]] — **confirmed**, fixed by `RATE_FRACTION`.
- [[hypotheses/tlb-hugepages]] — **refuted.** +1.7% at 1 MiB. Four hypotheses, four negatives; the mean question is closed.
- [[hypotheses/thp-is-not-hugepages]] — **confirmed negative.** `thp-advise` ≠ huge pages here.

## Traps — each of these cost real time once

- [[traps/touch-flag]] — the fake flat line. The worst one.
- [[traps/smt-siblings]] — 185× p99 inflation.
- [[traps/windows-git-add-all]] — clobbered a dataset once.
- [[traps/env-var-spacing]] — `REPS` silently ignored, twice.
- [[traps/stale-clone]] — three failed runs from a wrong `~/zcring`.
- [[traps/dont-overwrite-results]] — destroyed the fan-out baseline once.
- [[traps/no-pasted-credentials]] — standing security rule.
- [[traps/i-cannot-fetch]] — my sandbox has no git credentials. Never claim sync.
- [[traps/no-git-index-writes]] — my `git status` leaves a lock I can't delete. Read-only git only.

## Submission

- [[submission/stage1-checklist]] — what's done, what's owed, what's blocking.

## Sessions

- [[sessions/2026-08-14]] — most recent. **Measurement laptop died; Layer 3 designed.**
- [[sessions/2026-08-09]]

## My own error log

Things I got wrong in this project, so I stop repeating the *kind* of mistake:

1. **Invented a deadline** ("abstract due 8–10 Aug") and restated it as fact.
   → Dates come from `HACKATHON.md` only. [[submission/stage1-checklist]]
2. **Declared the repos "in sync"** after a `git fetch` that had silently
   failed. → [[traps/i-cannot-fetch]]
3. **Pushed a hypothesis twice after it was disconfirmed.**
   → [[hypotheses/busy-spin-power]]
4. **Gave a command that overwrote committed data.**
   → [[traps/dont-overwrite-results]]
5. **Claimed a "~2× floor" from VM data**, then a "1.1–1.5× win" from a
   conflicting C-state config. Both retracted. → [[claims/large-payload-n1]]
6. **Read "p99.9 is 2.1× better" off five reps.** Ten reps reversed the sign.
   → [[claims/large-payload-tail]]

The pattern in all six: asserting where I should have measured or checked.
#6 adds a specific one — **a tail statistic needs its per-rep spread looked
at before its mean**, and n=5 cannot characterise a p99.9.

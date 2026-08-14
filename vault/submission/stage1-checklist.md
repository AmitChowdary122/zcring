---
status: pending
confidence: high
source: HACKATHON.md
verified: 2026-08-09
---

# Stage 1 submission — state

> **`HACKATHON.md` is the only authority on dates and form fields.** I once
> invented an "8–10 Aug abstract deadline" and restated it as fact. If a date
> is not in `HACKATHON.md`, I do not have it.

## Dates

| Date | What |
|---|---|
| **~18–20 Aug** | *Self-imposed* target — everything uploaded, buffer left |
| **~23–24 Aug** | **Stage 1 closes.** Full submission, not an abstract |
| **25 Aug** | Abstract + PS selection closes. **Real code freeze.** |
| **1st week Sep** | Online presentation to judges — **this decides the winner** |

**The trap in the official schedule:** the prototype demo follows the abstract
by about a week, so there is *no build window between the two gates*. Treat
25 Aug as the date everything must work by.

## Checklist

| Item | State |
|---|---|
| Architecture diagram | ✅ `docs/architecture.png`, 254 KB (cap 300 KB) |
| Presentation | ✅ `docs/zcring_deck.pdf`, 85 KB, 11 slides |
| Public GitHub link | ❌ repo still private — [[decisions/repo-stays-private]] |
| Form text — Title, Objective, Description, Novelty, Innovation, Tech Stack | ❌ **not started — next task** |
| Model Type | **"Inbuilt Model"** — [[decisions/ai-criterion-answer]] |
| Demo video | ❌ not started |

Both image caps are **300 KB** and brutal. Diagram and deck are scripted
(`docs/architecture.py`, `docs/deck.py`) so they regenerate — check size after
any edit.

## Priority order

1. **Form text.** Half a day. `ABSTRACT.md` has the raw material and the deck
   has settled the narrative. *Opus.*
2. **Demo video.** Reserved effort, not yet scoped.
3. **Repo public** — submission-day step, easy to forget.
4. **Layer 3** — [[decisions/layer3-is-the-novelty]]. Largest remaining swing,
   scores on Novelty *and* Security. Only start with two clear weeks left.

Deliberately after those: producer crash recovery, `eventfd`/`epoll` bridge,
`isolcpus`/`nohz_full`/PREEMPT_RT for [[claims/determinism-p999]], iceoryx and
ZeroMQ comparators.

## The story the submission tells

Lead with [[claims/64b-small-message]] (stating the spin qualifier), disclose
[[claims/large-payload-n1]] before anyone finds it, then win it back with
[[claims/fanout-crossover]]. Close with [[claims/determinism-p999]] and
[[claims/reproducibility]] — the two things most competing submissions will
not have.

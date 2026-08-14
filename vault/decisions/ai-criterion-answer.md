---
status: decided
confidence: high
source: src/zcring.h §§5-10
verified: 2026-08-08
---

# The AI criterion — adaptive notification, and never call it "AI"

**AI/Technical Approach is a CORE rubric criterion** (see `HACKATHON.md`). The
project scored zero on it until 8 Aug. It now has an honest answer.

**Model Type on the submission form: "Inbuilt Model".**

## What it actually is

The spin-then-futex threshold is **learned online from the observed
inter-arrival distribution**, rather than being a compiled-in constant.

- Constrained objective: minimise `L(S) = W·(1−F(S))` subject to
  `C(S) = E[min(X,S)] ≤ β·E[X]`
- Robbins–Monro multiplicative quantile update
- Ski-rental 2-competitive floor: `S ≥ measured W`
- ε-greedy exploration, because a policy that never over-spins never learns
  the tail it is trying to avoid
- Censored-sample de-biasing via a producer `wake_ts` stamp

Full derivation in `src/zcring.h` §§5–10, written to be read by a judge.

## Framing rules

- Call it **"an online-learned adaptive policy"**. Never "AI".
- **Do not bolt on an LLM.** It would be transparently decorative and a judge
  on a *kernel* hackathon panel will say so.
- The strength of this answer is that it is *load-bearing* — the system
  genuinely needs the threshold and a fixed guess genuinely does worse. That
  is a much better story than a bolted-on model, and it is defensible under
  questioning because the maths is in the header.

## Why it exists at all

A fixed spin count is a guess about a distribution the framework cannot know
at compile time. `--yield`/`YIELD_SPINS` were exactly that guess, and they
were removed when this landed. See [[decisions/spin-is-the-sweep-default]] for
what the two waiter modes now mean.

ABI stayed v2 (statically asserted). TSan clean.

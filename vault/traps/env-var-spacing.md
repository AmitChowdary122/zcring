---
status: decided
confidence: high
verified: 2026-08-08
---

# Env vars in a pasted command — check the spacing

Twice now, `REPS` was silently ignored because a line wrapped and two
assignments ran together:

```bash
OUT_SUFFIX=_notifyREPS=3 ./scripts/sweep.sh     # REPS defaulted to 1
```

The run completed, wrote `results/sweep_notifyREPS=3.csv`, and looked fine.
**A silently-defaulted `REPS=1` is not a measurement** — the project standard
is REPS=5 minimum ([[claims/machine]]).

## Rules for commands I hand over

- Give **one `&&`-chained line**, not a multi-line block that might wrap badly.
- Put env assignments on the same line as the command, spaced clearly.
- Where it matters, have the script **echo what it received**.
- Include `cd <path> &&` at the front — see [[traps/stale-clone]].

The salvaged file is `results/sweep_notify_reps1.csv`, renamed to say what it
actually is rather than what it was asked to be. Same principle as
[[hypotheses/thp-is-not-hugepages]].

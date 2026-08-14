---
status: decided
confidence: high
verified: 2026-08-09
---

# Never `git add -A` in the Windows working tree

**Stage files by name there. Always.**

That tree only ever holds current doc edits; **everything else in it is
stale**. A blind `git add -A` once silently reverted a Makefile fix and
clobbered a bare-metal dataset that had cost an hour of quiet-machine time.

```bash
# correct
git add CLAUDE.md README.md docs/deck.py
```

## Related

- `results/` is deliberately **not** in `.gitignore` and **not** removed by
  `make clean`. Raw benchmark data is committed evidence. Both files carry a
  comment saying so — don't "tidy" either.
- This vault **is** gitignored. A `git add -A` here would push it, which is
  the reason [[00-how-i-use-this]] says to keep the tone publishable anyway.
- The Ubuntu tree is the one that builds and measures; the Windows tree is
  docs only. When they diverge, [[traps/i-cannot-fetch]] applies.

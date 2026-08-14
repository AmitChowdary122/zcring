---
type: protocol
---

# How I use this vault

## Reading, at session start

1. Read `HACKATHON.md` if the task touches rules, dates, or the submission form.
2. Read `vault/INDEX.md`.
3. Open **only** the notes the task names. Do not bulk-read the vault — the
   whole point is that the index tells me what I can skip.
4. If the task involves current build state or anything measured on Ubuntu,
   read `STATUS.md` too. **The vault can be stale about Ubuntu work**, because
   it is gitignored and that machine never writes to it.

## Writing, at session end

The user says when the session is ending. Then:

1. Append a dated note to `vault/sessions/`.
2. Update any claim, hypothesis, or decision note the session changed —
   **edit the existing note, never add a second one on the same subject.**
   Duplicated facts that drift apart are the failure mode this vault exists
   to prevent.
3. If something was *retracted*, set `status: superseded`, keep the note, and
   say what replaced it. Deleted mistakes get remade.
4. If it also matters on the Ubuntu machine, write it to `STATUS.md` as well.
   That file is the only channel between the two boxes.
5. Add a line to my error log in `INDEX.md` if I got something wrong.

## Frontmatter

```yaml
status:     measured | refuted | pending | superseded | decided
confidence: high | medium | low
source:     results/<file>.csv        # or the repo file that proves it
verified:   YYYY-MM-DD
```

`status` and `source` are the two that matter. **A claim with no `source` is
not a claim, it is a guess** — mark it `confidence: low` and say so out loud
when I use it.

## Rules for this vault specifically

- **One fact per file.** So I can load three notes instead of a 500-line doc.
- **Short.** The prose lives in `README.md` and `src/zcring.h`. Notes here are
  the fact, the evidence, and what would overturn it.
- **Link liberally.** A wiki-link to a note that doesn't exist yet is a
  to-do, not an error — but run `python3 vault/check.py` at session end so
  the to-dos are deliberate rather than typos.
- **Never copy numbers between notes.** One note owns each number; others link
  to it. Copied numbers are how the "~2× floor" survived three documents.
- **Publishable tone.** It's gitignored today, but a stray `git add -A` is
  exactly the trap in [[traps/windows-git-add-all]]. Write nothing here I'd
  mind a judge reading.

## This vault is committed — and must be untracked before the repo goes public

Changed 14 Aug. The measurement laptop died and its Ubuntu SSD now dual-boots
in the same machine as Windows, so sessions move between the two and a
Windows-only vault would arrive empty on whichever box is doing the work.
It therefore travels through git like everything else.

**The repo is private until submission** ([[decisions/repo-stays-private]]).
Before flipping it public:

```bash
git rm -r --cached vault/          # stop tracking; files stay on disk
# then re-add `vault/` to .gitignore
git commit -m "Untrack working vault ahead of publication"
```

History will still contain it — that is accepted. The tone rule below is what
actually protects this, not the untracking.

Because it is committed, it now has version history, and a fresh clone has it.
Everything in it is still reconstructible from `STATUS.md`, `reports.txt`,
`README.md` and `CLAUDE.md` — this vault is an *index over* the repo's memory,
not a replacement for it.

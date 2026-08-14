---
status: decided
confidence: high
verified: 2026-08-09
---

# Never run index-touching git commands against the Windows tree

My sandbox mounts the Windows working tree over a filesystem where I can
**create** `.git/index.lock` but not **unlink** it:

```
warning: unable to unlink '.../.git/index.lock': Operation not permitted
```

A plain `git status` was enough. The lock then blocked the user's next commit
with *"Another git process seems to be running"* — and I could not clear it,
so it cost a round trip and a confusing error.

## Safe from my sandbox

`git log`, `git show`, `git cat-file`, `ls`, `grep`, `cat`, `wc` — anything
read-only that does not touch the index.

## Not safe

`git status`, `git add`, `git diff` (refreshes the index), `git stash`,
`git fetch`/`pull` (also blocked anyway — [[traps/i-cannot-fetch]]).

## Instead

To learn what changed, compare **file contents**: `ls -la`, `grep`, `wc -l`,
`git log --oneline`. That is the same discipline
[[traps/i-cannot-fetch]] already requires — verify by contents, not by git
metadata.

If a lock does get left behind, only the user can remove it:

```bash
rm -f /mnt/c/Users/amitc/Documents/SSM_CDAC_Hackathon/.git/index.lock
```

It is a zero-byte file with nothing holding it, so removing it is safe.

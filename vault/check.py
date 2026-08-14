#!/usr/bin/env python3
"""Vault integrity check. Run at session end: python3 vault/check.py

Three failure modes this catches, all of which have real analogues in this
project's history:

  1. Broken wiki-links       -- a typo'd link silently points nowhere, so a
                                future session never finds the note it needed.
  2. Claims with no source   -- a claim without evidence is a guess. This repo
                                has already carried three retracted claims that
                                survived because nothing forced them to name
                                their data. See vault/claims/large-payload-n1.md.
  3. Stale-dated notes       -- not an error, just a report, so old `verified:`
                                dates are visible rather than assumed current.
"""
import os
import re
import sys
from datetime import date

VAULT = os.path.dirname(os.path.abspath(__file__))
NEEDS_SOURCE = ("claims", "hypotheses")   # dirs where a bare assertion is a bug


def notes():
    for root, _, files in os.walk(VAULT):
        for f in sorted(files):
            if f.endswith(".md"):
                yield os.path.join(root, f)


def rel(p):
    return os.path.relpath(p, VAULT).replace(os.sep, "/")


def frontmatter(text):
    m = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    if not m:
        return {}
    out = {}
    for line in m.group(1).splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def main():
    index, problems, report = {}, [], []

    for p in notes():
        r = rel(p)
        index[r[:-3]] = r
        index.setdefault(os.path.basename(r)[:-3], r)

    for p in notes():
        r = rel(p)
        text = open(p, encoding="utf-8").read()
        fm = frontmatter(text)

        for target in re.findall(r"\[\[([^\]|#]+)", text):
            if target.strip() not in index:
                problems.append(f"broken link  {r} -> [[{target}]]")

        top = r.split("/")[0]
        if top in NEEDS_SOURCE:
            status = fm.get("status", "")
            if status in ("measured", "confirmed", "refuted") and not fm.get("source"):
                problems.append(f"no source    {r} (status: {status})")
            if not status:
                problems.append(f"no status    {r}")

        if fm.get("verified"):
            try:
                y, m, d = (int(x) for x in fm["verified"].split("-"))
                age = (date.today() - date(y, m, d)).days
                if age > 14:
                    report.append(f"  {age:>3}d  {r}")
            except ValueError:
                problems.append(f"bad date     {r}: {fm['verified']}")

    print(f"{len(set(index.values()))} notes")
    if report:
        print("\nnot verified in over two weeks:")
        print("\n".join(sorted(report, reverse=True)))
    if problems:
        print(f"\n{len(problems)} problem(s):")
        for x in problems:
            print("  " + x)
        return 1
    print("\nno problems")
    return 0


if __name__ == "__main__":
    sys.exit(main())

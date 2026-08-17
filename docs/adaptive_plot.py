#!/usr/bin/env python3
"""Generate docs/adaptive_trace.pdf — visual evidence for the AI/Technical
Approach rubric criterion.

src/zcring.h §§5-10 derives ~500 lines of argument for why the spin-then-
futex threshold is learned online rather than configured. Until this script,
that argument had no picture. This is the picture: the learned spin budget
S actually tracking a producer whose offered inter-arrival gap changes mid-
run, against the ski-rental floor and a fixed budget that cannot adapt.

Vector PDF, matching docs/deck.py's approach and palette (not its slide
layout — this is one standalone chart, not a deck page): drawn, not
rasterised, so a run with 2,000 samples across three series stays tens of
KB, comfortably under the 150 KB budget.

    python3 docs/adaptive_plot.py
"""
import csv
import statistics
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

INK, MUTED, FAINT = "#14161a", "#5a6070", "#9aa0ac"
ZC, CMP, WARN, OK = "#1baf7a", "#8a8f99", "#c98500", "#2a78d6"
GRID = "#d5d9e0"
W, H = 13.333, 7.5  # 16:9, same canvas as deck.py

SRC = "results/adaptive_trace.csv"
OUT = "docs/adaptive_trace.pdf"

rows = list(csv.DictReader(open(SRC)))
for r in rows:
    for k in ("phase_us", "seq", "spin_ns", "gap_ewma_ns", "wake_ewma_ns", "burn_ewma_ns"):
        r[k] = int(r[k])
    r["t_ms"] = float(r["t_ms"])

seq       = [r["seq"] for r in rows]
spin      = [r["spin_ns"] for r in rows]
wake      = [r["wake_ewma_ns"] for r in rows]
phase_us  = [r["phase_us"] for r in rows]

# Phase boundaries: first index at which the offered gap changes.
boundaries = [0]
for i in range(1, len(rows)):
    if phase_us[i] != phase_us[i - 1]:
        boundaries.append(i)
boundaries.append(len(rows))
phases = [(boundaries[i], boundaries[i + 1], phase_us[boundaries[i]])
          for i in range(len(boundaries) - 1)]


def fmt_gap(us):
    if us >= 1000:
        return f"{us / 1000:g} ms gap"
    return f"{us} µs gap"


def rolling_median(xs, k=15):
    out = []
    half = k // 2
    for i in range(len(xs)):
        lo, hi = max(0, i - half), min(len(xs), i + half + 1)
        out.append(statistics.median(xs[lo:hi]))
    return out


spin_smooth = rolling_median(spin)

# "Fixed budget" baseline: what a one-time calibration against the FIRST
# regime would have frozen -- the median learned S over the back half of
# phase 1, held flat for the whole run. Derived from this same trace, not
# invented, so the comparison is against a real number the policy itself
# produced, not a strawman.
p1_lo, p1_hi, _ = phases[0]
fixed_budget = statistics.median(spin[p1_lo + (p1_hi - p1_lo) // 2 : p1_hi])

fig = plt.figure(figsize=(W, H))
fig.patch.set_facecolor("white")

ax_head = fig.add_axes([0, 0, 1, 1])
ax_head.set_xlim(0, 100)
ax_head.set_ylim(0, 100)
ax_head.axis("off")
ax_head.text(6, 93, "The adaptive spin budget tracks the arrival process online",
             fontsize=23, fontweight="bold", color=INK, va="top")
ax_head.text(6, 87,
             "Learned per-consumer from inter-arrival gaps that shift mid-run — nothing here is a configured constant",
             fontsize=13.5, color=MUTED, va="top")
ax_head.plot([6, 94], [83.5, 83.5], color=GRID, lw=1.2)
note_lines = "\n".join([
    f"Raw data: {SRC} ({'scripts/adaptive_trace.sh, bench/adaptive_trace.c'}). Single run, one consumer, unquieted machine —",
    "a policy-shape demonstration, not a quoted latency claim. Fixed-budget line: this trace's own median S over the back half of phase 1, held flat.",
])
ax_head.text(6, 2, note_lines, fontsize=10, color=FAINT, va="bottom",
             style="italic", linespacing=1.7)

axc = fig.add_axes([0.075, 0.24, 0.90, 0.54])

label_transform = axc.get_xaxis_transform()  # x: data coords, y: axes fraction
for lo, hi, gap in phases:
    if lo > 0:
        axc.axvline(lo, color=GRID, lw=1.1, zorder=1)
    axc.text((lo + hi) / 2, 1.02, fmt_gap(gap), fontsize=11, color=MUTED,
              ha="center", va="bottom", transform=label_transform)

axc.plot(seq, spin, color=ZC, lw=0.6, alpha=0.3, zorder=2)
axc.plot(seq, spin_smooth, color=ZC, lw=2.4, zorder=4,
          label="learned spin budget S (15-sample rolling median)")
axc.plot(seq, wake, color=WARN, lw=1.8, ls="--", zorder=3,
          label="ski-rental floor (measured wake cost W)")
axc.axhline(fixed_budget, color=CMP, lw=2.0, ls=":", zorder=3,
             label=f"fixed budget calibrated once on phase 1 ({fixed_budget:,.0f} ns)")

axc.set_yscale("log")
axc.set_xlim(0, len(rows))
axc.set_xlabel("wait number", fontsize=12, color=MUTED)
axc.set_ylabel("nanoseconds (log)", fontsize=12, color=MUTED)
axc.tick_params(labelsize=10, colors=MUTED)
axc.legend(fontsize=11, frameon=False, loc="upper left")
for sp in ("top", "right"):
    axc.spines[sp].set_visible(False)
for sp in ("left", "bottom"):
    axc.spines[sp].set_color(GRID)
axc.grid(axis="y", color=GRID, lw=0.6, alpha=0.6)

fig.savefig(OUT)
plt.close(fig)

import os
print(f"wrote {OUT} ({os.path.getsize(OUT) / 1024:.1f} KB)")

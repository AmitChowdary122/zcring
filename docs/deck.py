#!/usr/bin/env python3
"""Generate docs/zcring_deck.pdf — the Stage 1 presentation.

Vector PDF, not PPTX: the portal caps the upload at 300 KB and accepts PDF,
and a deck with embedded raster charts cannot fit. Everything here is drawn,
so the whole deck is tens of KB and every figure is generated from the
committed CSVs rather than retyped.

    python3 docs/deck.py
"""
import csv, collections, statistics
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Rectangle, FancyBboxPatch

INK, MUTED, FAINT = "#14161a", "#5a6070", "#9aa0ac"
ZC, CMP, WARN, OK = "#1baf7a", "#8a8f99", "#c98500", "#2a78d6"
W, H = 13.333, 7.5          # 16:9

def load(path, key, col="p50_ns"):
    d = collections.defaultdict(list)
    for r in csv.DictReader(open(path)):
        d[tuple(r[k] for k in key)].append(int(r[col]))
    return {k: statistics.mean(v) for k, v in d.items()}

sweep  = load("results/sweep.csv", ["transport", "size"])
fan_y  = load("results/fanout_yield_historical.csv", ["transport","consumers","size"])
fan_n  = load("results/fanout_notify.csv", ["transport","consumers","size"])

def slide(pdf, title, kicker=None):
    fig = plt.figure(figsize=(W, H)); fig.patch.set_facecolor("white")
    ax = fig.add_axes([0, 0, 1, 1]); ax.set_xlim(0, 100); ax.set_ylim(0, 100); ax.axis("off")
    ax.text(6, 90, title, fontsize=27, fontweight="bold", color=INK, va="top")
    if kicker:
        ax.text(6, 83.5, kicker, fontsize=14.5, color=MUTED, va="top")
    ax.plot([6, 94], [79, 79], color="#e3e6ea", lw=1.2)
    return fig, ax

def bullets(ax, items, y=71, x=6, size=14.5, step=7.5, wrap=96, lead_gap=4.2):
    """Render bullets. `**lead** rest` puts the lead on its own bold line with
    the remainder beneath it — reads better on a slide than inline bolding,
    and avoids matplotlib's lack of rich text in a single call."""
    import re, textwrap
    for it in items:
        m = re.match(r"^\*\*(.+?)\*\*\s*(.*)$", it, re.S)
        if m:
            lead, rest = m.group(1).strip(), m.group(2).strip()
            ax.text(x, y, textwrap.fill(lead, wrap), fontsize=size, color=INK,
                    fontweight="bold", va="top", linespacing=1.45)
            y -= lead_gap
            if rest:
                w = textwrap.fill(rest, wrap + 4)
                ax.text(x, y, w, fontsize=size - 0.7, color=MUTED, va="top",
                        linespacing=1.5)
                y -= step * (0.55 + w.count("\n") * 0.62)
            else:
                y -= step * 0.25
        else:
            w = textwrap.fill(it, wrap)
            ax.text(x, y, w, fontsize=size, color=MUTED, va="top", linespacing=1.5)
            y -= step * (0.7 + w.count("\n") * 0.62)
        y -= 2.2
    return y

def note(ax, text, y=9):
    ax.text(6, y, text, fontsize=11.5, color=FAINT, va="bottom", style="italic",
            linespacing=1.6)

pdf = PdfPages("docs/zcring_deck.pdf")

# ---------------------------------------------------------------- 1. title --
fig = plt.figure(figsize=(W, H)); fig.patch.set_facecolor("white")
ax = fig.add_axes([0,0,1,1]); ax.set_xlim(0,100); ax.set_ylim(0,100); ax.axis("off")
ax.add_patch(Rectangle((0, 0), 1.6, 100, facecolor=ZC, edgecolor="none"))
ax.text(8, 62, "zcring", fontsize=58, fontweight="bold", color=INK)
ax.text(8, 53, "Zero-copy shared-memory IPC for embedded Linux",
        fontsize=22, color=INK)
ax.text(8, 45.5, "One producer publishes once. N consumers read the same bytes in place.\n"
                 "No memcpy and no system call on the data path.",
        fontsize=15, color=MUTED, linespacing=1.7, va="top")
ax.text(8, 26, "SSM / C-DAC Next-Gen Kernel Hackathon  ·  Track 1", fontsize=13, color=MUTED)
ax.text(8, 21, "Problem statement: Zero-Copy Shared-Memory IPC Framework for Embedded Linux",
        fontsize=12.5, color=FAINT)
ax.text(8, 13, "C11 · no external dependencies · ThreadSanitizer clean · every figure traceable to committed raw data",
        fontsize=11.5, color=FAINT)
pdf.savefig(fig); plt.close(fig)

# -------------------------------------------------------------- 2. problem --
fig, ax = slide(pdf, "The cost is copies — and worse, it is unpredictable",
                "Conventional Linux IPC moves every message through the kernel twice")
y = bullets(ax, [
 "**Four passes over memory per message.** The producer writes it, the kernel copies it in, the kernel copies it out, the consumer reads it. Two of those four are pure overhead.",
 "**A system call on every send and every receive.** At small message sizes this, not the copying, is what dominates.",
 "**For real-time embedded work the variance matters more than the mean.** A control loop that is usually fast and occasionally late is a control loop that is late.",
], y=70, step=9)
ax.add_patch(FancyBboxPatch((6, 20), 88, 16, boxstyle="round,pad=0.6,rounding_size=0.8",
                            facecolor="#fdf3e0", edgecolor=WARN, lw=1.3))
ax.text(50, 28, "Sensor → controller → actuator pipelines need an answer on time, every time.\n"
                "Determinism is the requirement; throughput is a side effect.",
        fontsize=14.5, color="#7a5200", ha="center", va="center", linespacing=1.8)
pdf.savefig(fig); plt.close(fig)

# ------------------------------------------------------------- 3. approach --
fig, ax = slide(pdf, "Share the memory instead of copying through it",
                "A lock-free MPMC ring over a memfd-backed mapping")
rows = [("producer writes payload", "1", "1  (into a staging buffer)"),
        ("copy into kernel", "—", "1"),
        ("copy out of kernel", "—", "1"),
        ("consumer reads payload", "1", "1")]
ax.text(10, 68, "passes over memory, per message", fontsize=13, color=MUTED)
ax.text(56, 68, "zcring", fontsize=14, color=ZC, fontweight="bold", ha="center")
ax.text(76, 68, "pipe / unix socket", fontsize=14, color=MUTED, fontweight="bold", ha="center")
yy = 62
for n, a, b in rows:
    ax.text(10, yy, n, fontsize=14, color=INK, va="center")
    ax.text(56, yy, a, fontsize=14, color=ZC, ha="center", va="center")
    ax.text(76, yy, b, fontsize=14, color=MUTED, ha="center", va="center")
    yy -= 6
ax.plot([10, 86], [yy + 3, yy + 3], color="#d5d9e0", lw=1.1)
ax.text(10, yy - 1, "total", fontsize=15, fontweight="bold", color=INK, va="center")
ax.text(56, yy - 1, "2", fontsize=19, fontweight="bold", color=ZC, ha="center", va="center")
ax.text(76, yy - 1, "4", fontsize=19, fontweight="bold", color=MUTED, ha="center", va="center")
bullets(ax, [
 "**reserve() / commit() and acquire() / release() hand back raw pointers into shared memory.** The application constructs and reads messages in place. There is no memcpy anywhere in the data path.",
], y=27, step=8)
note(ax, "Architecture diagram submitted separately · Vyukov sequence scheme · head and tail on separate cache lines")
pdf.savefig(fig); plt.close(fig)

# ------------------------------------------------- 4. small-message result --
fig, ax = slide(pdf, "Small messages: 18–20× lower latency",
                "One-way p50, bare metal, dual-core + SMT, every payload byte written and read")
sizes = ["64","256","1024","4096","16384","65536"]
labels = ["64 B","256 B","1 KiB","4 KiB","16 KiB","64 KiB"]
zc_v = [sweep[("zcring", s)]/1000 for s in sizes]
cm_v = [min(sweep[("pipe", s)], sweep[("unix", s)])/1000 for s in sizes]
axc = fig.add_axes([0.10, 0.20, 0.52, 0.50])
xs = range(len(sizes))
axc.bar([x-0.2 for x in xs], cm_v, 0.4, color=CMP, label="best of pipe / unix")
axc.bar([x+0.2 for x in xs], zc_v, 0.4, color=ZC, label="zcring")
axc.set_yscale("log"); axc.set_xticks(list(xs)); axc.set_xticklabels(labels, fontsize=11)
axc.set_ylabel("p50 latency (µs, log)", fontsize=11.5, color=MUTED)
axc.legend(fontsize=11, frameon=False); axc.tick_params(labelsize=10, colors=MUTED)
for sp in ("top","right"): axc.spines[sp].set_visible(False)
for sp in ("left","bottom"): axc.spines[sp].set_color("#d5d9e0")
tbl = [("64 B", "114 ns", "18–20×"), ("1 KiB", "233 ns", "~10×"),
       ("4 KiB", "749 ns", "3.7×"), ("16 KiB", "3.6 µs", "1.55×")]
ax.text(68, 66, "zcring p50        vs best", fontsize=12, color=MUTED)
yy = 60
for a, b, c in tbl:
    ax.text(68, yy, a, fontsize=13.5, color=MUTED, va="center")
    ax.text(80, yy, b, fontsize=13.5, color=INK, va="center", ha="right")
    ax.text(93, yy, c, fontsize=13.5, color=ZC, fontweight="bold", va="center", ha="right")
    yy -= 5.5
ax.text(68, 34, "Quoted as a range because the entire\nsuite was re-measured on a separate\noccasion; zcring reproduced within 2%.",
        fontsize=11.5, color=FAINT, va="top", linespacing=1.7)
note(ax, "Dedicated-core (polling) configuration — see next slide. Raw data: results/sweep.csv")
pdf.savefig(fig); plt.close(fig)

# ------------------------------------------------------ 5. the honest trade --
fig, ax = slide(pdf, "Two deployment postures, stated up front",
                "The small-message win comes from removing the syscall — so it depends on not making one")
for i, (t, sub, col, vals) in enumerate([
    ("Dedicated core  (spin)", "the consumer polls the ring", ZC,
     ["64 B:  114 ns   ·   18–20× vs pipe",
      "lowest achievable latency",
      "costs one core, continuously",
      "standard practice in real-time embedded"]),
    ("Shared core  (adaptive notify)", "the consumer blocks when blocking pays", OK,
     ["64 B:  2.3 µs   ·   ~parity with pipe",
      "idle CPU cost comparable to a socket",
      "syscall returns, so the small-message edge goes",
      "the copy advantage is unaffected"])]):
    x = 6 + i * 45
    ax.add_patch(FancyBboxPatch((x, 26), 42, 46, boxstyle="round,pad=0.6,rounding_size=0.8",
                                facecolor="white", edgecolor=col, lw=1.6))
    ax.text(x + 21, 67, t, fontsize=16, fontweight="bold", color=INK, ha="center")
    ax.text(x + 21, 62.5, sub, fontsize=12, color=MUTED, ha="center")
    yy = 55
    for v in vals:
        ax.text(x + 3, yy, "·  " + v, fontsize=12.5, color=MUTED, va="center")
        yy -= 6.5
ax.text(50, 18, "Both are shipped. The framework does not pick for you — the deployment does.",
        fontsize=14.5, color=INK, ha="center", fontweight="bold")
note(ax, "The 18–20× headline is a dedicated-core number and is labelled as such everywhere it appears.")
pdf.savefig(fig); plt.close(fig)

# ------------------------------------------------------------ 6. fan-out ----
fig, ax = slide(pdf, "Scalability: publication is O(1) in consumer count",
                "One release store regardless of N — copying transports pay N copies each way")
axc = fig.add_axes([0.09, 0.22, 0.46, 0.46])
ns = ["1","2","4"]
zc_l = [fan_n[("zcring", n, "1048576")]/1000 for n in ns]
cm_l = [min(fan_n[("pipe", n, "1048576")], fan_n[("unix", n, "1048576")])/1000 for n in ns]
axc.plot([1,2,4], cm_l, "o--", color=CMP, lw=2, ms=8, label="best of pipe / unix")
axc.plot([1,2,4], zc_l, "o-", color=ZC, lw=2.4, ms=9, label="zcring")
axc.set_xticks([1,2,4]); axc.set_xlabel("consumers", fontsize=11.5, color=MUTED)
axc.set_ylabel("p50 latency (µs) at 1 MiB", fontsize=11.5, color=MUTED)
axc.legend(fontsize=11, frameon=False); axc.tick_params(labelsize=10, colors=MUTED)
for sp in ("top","right"): axc.spines[sp].set_visible(False)
for sp in ("left","bottom"): axc.spines[sp].set_color("#d5d9e0")
ax.text(60, 66, "1 MiB payload, adaptive notify", fontsize=13, color=MUTED)
for i,(n,v,c) in enumerate([("N = 1","0.82×",WARN),("N = 2","1.36×",ZC),("N = 4","1.24×",FAINT)]):
    ax.text(60, 59 - i*7, n, fontsize=14, color=MUTED, va="center")
    ax.text(74, 59 - i*7, v, fontsize=17, color=c, fontweight="bold", va="center")
ax.text(60, 30, "zcring loses at one consumer and wins\nfrom two onward. The crossover is the\nresult — not a flat multiplier.",
        fontsize=13, color=INK, va="top", linespacing=1.7)
note(ax, "N=4 on two physical cores is oversubscribed — four consumers wake simultaneously onto two cores.\n"
         "Not quoted as a scalability result in either direction; it needs a machine with more cores to measure honestly.")
pdf.savefig(fig); plt.close(fig)

# -------------------------------------------------------- 7. determinism ----
fig, ax = slide(pdf, "Determinism: we found the jitter and removed it",
                "Tail latency traced to a hardware cause, not tuned away")
ax.text(6, 70, "Deep CPU idle states on this part:", fontsize=14, color=INK, va="top")
for i,(n,v) in enumerate([("POLL","0 µs"),("C1","1 µs"),("C2","253 µs"),("C3","1048 µs")]):
    ax.text(9, 63 - i*5.5, n, fontsize=13, color=MUTED, va="center")
    ax.text(24, 63 - i*5.5, v, fontsize=13, color=INK if n!="C3" else WARN,
            fontweight="bold" if n=="C3" else "normal", va="center")
ax.text(6, 36, "C3's 1048 µs exit latency matched an observed\n1220 µs p99.9 almost exactly.",
        fontsize=13, color=MUTED, va="top", linespacing=1.7)
ax.add_patch(FancyBboxPatch((44, 30), 50, 42, boxstyle="round,pad=0.6,rounding_size=0.8",
                            facecolor="white", edgecolor=ZC, lw=1.6))
ax.text(69, 67, "p99.9 at 64 B, 5 repetitions", fontsize=13.5, color=MUTED, ha="center")
ax.text(51, 58, "idle states enabled", fontsize=13, color=MUTED)
ax.text(90, 58, "354 µs", fontsize=17, color=WARN, fontweight="bold", ha="right")
ax.text(51, 51, "range across reps", fontsize=12, color=FAINT)
ax.text(90, 51, "787 ns – 1.77 ms", fontsize=12.5, color=FAINT, ha="right")
ax.plot([49, 90], [45, 45], color="#e3e6ea", lw=1)
ax.text(51, 39, "deep idle disabled", fontsize=13, color=MUTED)
ax.text(90, 39, "2.4 µs", fontsize=19, color=ZC, fontweight="bold", ha="right")
ax.text(51, 33.5, "range across reps", fontsize=12, color=FAINT)
ax.text(90, 33.5, "1.4 – 4.2 µs", fontsize=12.5, color=FAINT, ha="right")
ax.text(50, 21, "The mean dropping is not the point. The variance collapsing is.",
        fontsize=15, color=INK, ha="center", fontweight="bold")
note(ax, "p99.99 still shows a smaller, separate source (IRQ / scheduling jitter). Disclosed, and the target of the isolcpus / PREEMPT_RT work.")
pdf.savefig(fig); plt.close(fig)

# ---------------------------------------------------- 8. adaptive policy ----
fig, ax = slide(pdf, "The notification threshold is learned, not configured",
                "An online-learned policy where a fixed constant cannot work")
bullets(ax, [
 "**The decision.** A consumer waiting on an empty ring must choose a spin budget S before observing the idle gap. Spin too long and CPU is wasted; spin too little and every message pays the block-and-wake cost.",
 "**The objective is constrained, not scalarised.** Minimise expected added latency W(1-F(S)) subject to expected spin cost <= beta*E[X]. Beta - the fraction of an idle gap the deployment will spend spinning - is the only input. Everything else is measured.",
 "**Estimator.** Robbins-Monro stochastic-approximation quantile, multiplicative step, two shifts and an add. Scale-free across decades of message rate, and deliberately non-annealing.",
], y=71, x=6, size=12.5, step=5.2, wrap=46, lead_gap=3.9)
bullets(ax, [
 "**Bias correction.** Waits that end in a block are censored samples. The producer stamps the wake time, which recovers the true arrival and stops the estimator being pushed toward spinning by the very outcome that should not do that.",
 "**Floor.** Ski-rental: S >= measured wake cost. 2-competitive and distribution-free, so the learner can never do worse than the classical bound.",
 "**Exploration.** Epsilon-greedy, because the greedy policy censors its own observations and cannot tell a gap just past S from one a hundred times past it. Cost bounded and charged to the same budget.",
], y=71, x=52, size=12.5, step=5.2, wrap=46, lead_gap=3.9)
note(ax, "Model type: inbuilt. Measured convergence at 100 µs pacing: gap estimate 97.8 µs, wake cost 2.07 µs, exploration 0.82% against a nominal 0.78%.\n"
         "At 1 MiB the same estimator learned 185 µs — the real inter-arrival, not the nominal pacing. That is the difference between measuring and configuring.")
pdf.savefig(fig); plt.close(fig)

# ------------------------------------------------------------- 9. method ----
fig, ax = slide(pdf, "How we know the numbers mean anything",
                "Six ways this benchmark could have lied, found and corrected")
left = [
 "**A harness that never touched the payload.** The first result was a perfect flat line, because only an 8-byte header moved. Every payload cache line is now written and read.",
 "**SMT-sibling pinning.** Two roles on one physical core left p50 looking fine and inflated p99 by 185x. The scripts now read the topology and refuse to do it.",
 "**Deep C-state entry.** A 1048 us idle-state exit latency landing in the tail. See previous slide.",
]
right = [
 "**Thermal throttling.** Sustained benchmarking heat-soaked a low-TDP part; large payloads degraded 70%. Every run is now gated on package temperature.",
 "**A saturating offered rate.** A 64 MiB ring against a 200 KB socket buffer at saturation measures queueing, not transport. Rate is now a stated fraction of measured saturation.",
 "**Orphaned consumers.** The textbook getppid()==1 check silently fails under a subreaper. Found by killing the producer and watching, not by reading code.",
]
bullets(ax, left,  y=71, x=6,  size=12.5, step=5.2, wrap=46, lead_gap=3.9)
bullets(ax, right, y=71, x=52, size=12.5, step=5.2, wrap=46, lead_gap=3.9)
note(ax, "Whole suite re-measured on a separate occasion from a clean boot: the fan-out result reproduced within 1.5%.\n"
         "Every figure in this deck is generated from committed CSVs by a script in the repository.")
pdf.savefig(fig); plt.close(fig)

# --------------------------------------------------------------- 10. demo ---
fig, ax = slide(pdf, "What it enables",
                "Camera → inference + display + recorder, one producer feeding three consumers")
ax.text(6, 70, "640×480 frames at 30 fps, three consumers, ten minutes, run against\n"
               "an identical pipeline over three UNIX sockets.",
        fontsize=14.5, color=INK, va="top", linespacing=1.7)
for i,(k,v,c) in enumerate([("frames delivered","18 000/18 000",INK),
                            ("frame rate, zero drift","30.000 fps",INK),
                            ("frames dropped","0",ZC),
                            ("memory traffic avoided","30.9 GB",ZC)]):
    ax.add_patch(Rectangle((6 + i*22, 30), 20, 20, facecolor="#f7f9fa", edgecolor="none"))
    ax.text(16 + i*22, 44, k, fontsize=11.5, color=MUTED, ha="center")
    ax.text(16 + i*22, 37, v, fontsize=14.5, color=c, fontweight="bold", ha="center",
            wrap=True)
ax.text(50, 21, "Three consumers read the same frame. A copying transport would move it three times.",
        fontsize=14.5, color=INK, ha="center", fontweight="bold")
note(ax, "make demo · runs unattended for ten minutes · resident memory flat, verified shared rather than duplicated via smaps_rollup")
pdf.savefig(fig); plt.close(fig)

# ------------------------------------------------------------- 11. limits ---
fig, ax = slide(pdf, "What it does not do, and what comes next",
                "Stated by us rather than found by you")
bullets(ax, [
 "**Single-consumer large payloads are slower than a UNIX socket.** 0.82x at 1 MiB, under the determinism-first configuration this project requires. Offered rate, thermal state and busy-spin were each eliminated as causes; the remaining candidates are platform power and frequency properties. Fan-out recovers the case from N=2.",
 "**Consumer count is bounded by core count, on any platform.** In broadcast mode every consumer runs on every message, so one producer plus four consumers oversubscribes a four-core embedded target exactly as it does this one. The crossover sits at N=2, which fits everywhere; we do not claim growth past it.",
 "**p99.99 is not yet quotable.** A smaller jitter source remains; isolcpus / nohz_full / PREEMPT_RT is the work that closes it.",
], y=71, size=13, step=5.6, wrap=98, lead_gap=4.2)
ax.add_patch(FancyBboxPatch((6, 12), 88, 20, boxstyle="round,pad=0.6,rounding_size=0.8",
                            facecolor="#fdf3e0", edgecolor=WARN, lw=1.3))
ax.text(50, 26, "Next: kernel-enforced arbitration", fontsize=16, fontweight="bold",
        color="#7a5200", ha="center")
ax.text(50, 18.5, "A pure-userspace framework cannot stop a buggy or malicious peer from corrupting the shared ring.\n"
                  "A kernel mediation layer closes exactly that gap - the one thing this design cannot do from userspace.",
        fontsize=13, color="#7a5200", ha="center", va="center", linespacing=1.8)
pdf.savefig(fig); plt.close(fig)

pdf.close()
print("wrote docs/zcring_deck.pdf")

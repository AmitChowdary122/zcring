---
status: decided
confidence: high
verified: 2026-08-09
---

# Scope is cut from the top, never the middle

```
Layer 4 (optional)  dma-buf / hardware buffer sharing
Layer 3 (stretch)   kernel module: access control + doorbell
Layer 2 (core)      adaptive notification, fan-out, crash recovery, epoll bridge
Layer 1 (DONE)      lock-free ring over memfd, in-place construction
```

Each layer is independently demoable. Nothing below depends on anything above.
**Complete Layers 1–2 beats broken 1–4.**

## Standing principles

1. Cut from the top, never the middle.
2. **A measured number beats a claimed one, everywhere.**
3. **The presentation is the product.** Code that can't be shown in ten
   minutes did not happen.
4. Acknowledge limitations before judges find them — it reads as rigor.
5. Score against the rubric, not against my own taste.

Principle 2 is the one this project has violated most often — see my error log
in [[INDEX]]. Principle 3 is why the final week is reserved for demo and
presentation, not coding.

## Current state

Layer 1 complete and validated: exactly-once delivery across 4 producers × 4
consumers over 200,000 messages, cross-process via inherited memfd,
**ThreadSanitizer clean**.

Layer 2: fan-out done, adaptive notification done. Outstanding — `eventfd`
bridge for `epoll` composition, producer crash recovery (a producer dying
mid-`reserve` currently leaks that slot; consumer death is handled by
`zc_bcast_reap()`).

Layer 3 is [[decisions/layer3-is-the-novelty]] and is worth more than pure
risk-management would suggest.

## Non-negotiable

`make test` before every commit. **`make tsan` after every change to
`src/zcring.h`** — memory-ordering bugs are invisible in normal runs and
surface as corruption weeks later.

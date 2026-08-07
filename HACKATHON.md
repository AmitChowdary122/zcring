# HACKATHON.md — the official rules and requirements

**This file outranks everything else in the repo.** `PLAN.md`, `README.md`,
and `CLAUDE.md` describe how we intend to win; this file describes what we are
actually being asked for and judged on. Where they disagree, this wins.

Captured 7 Aug 2026 from the organiser's submission page. These do not change.

---

## Event

**Next-Gen Kernel Hackathon: Secure Design, Isolation & Optimized Performance
with Secure Boot, Toolchains and Hardening (Linux Based)**

Problem statement selected (**cannot be changed**):

> **Zero-Copy Shared-Memory IPC Framework for Embedded Linux** — Develop a
> zero-copy shared-memory IPC framework for Embedded Linux to reduce latency,
> eliminate unnecessary memory copies, and provide deterministic,
> high-performance communication for real-time embedded applications.

## Stages

**Stage 1 — Submission & Screening.** Teams submit solution proposal, system
architecture, source-code repository, complete documentation and a
demonstration video via the portal. A panel of subject-matter experts
evaluates and shortlists. Number shortlisted per problem statement is at the
organisers' and jury's sole discretion.

**Stage 2 — Online Technical Presentation.** Shortlisted teams present to an
expert jury: solution walkthrough, **live demonstration of the working
prototype**, and Q&A. Judged on technical depth, innovation, feasibility,
scalability, **quality of AI integration**, implementation quality, and impact.

**Stage 3 — Prize Distribution.** Winners announced; certificates and prizes,
with possible mentorship, incubation support or pilot engagement with C-DAC.

## Stage 1 submission form — every required field

| Field | Required | Status |
|---|---|---|
| Title | ✅ | — |
| Problem Statement | ✅ | fixed, see above |
| Objective | ✅ | — |
| Description | ✅ | — |
| Novelty | ✅ | — |
| Innovation | ✅ | — |
| Data Set Used | optional | likely N/A |
| **Architecture Diagram** | ✅ | **JPG/JPEG/PNG, max 300 KB — does not exist yet** |
| Tech Stack | ✅ | — |
| **Model Type** | ✅ | **Open Source Model *or* Inbuilt Model — see AI note below** |
| Deployment Link | optional | N/A |
| **GitHub Link** | ✅ | **repo is currently PRIVATE — must be reachable** |
| **Presentation (PPT)** | ✅ | **PDF/PPT/PPTX, max 300 KB — does not exist yet** |
| Demo Video | optional | strongly advised — see Implementation Quality |

**Two file-size limits are brutal and will bite late:** the architecture image
and the presentation are each capped at **300 KB**. A normal PPTX with
screenshots blows past that instantly. Plan for vector/PDF export, few images,
aggressive compression — and test the size early, not on submission day.

## Evaluation criteria (the real rubric)

### Core

- **Novelty & Innovation** — originality of the idea and the innovative
  aspects described.
- **Technical Feasibility & Architecture** — soundness, modularity and clarity
  **of the submitted Architecture Image**. The diagram is not decoration; it is
  directly scored.
- **AI / Technical Approach** — depth of AI/ML techniques, including
  justification for open-source or in-house models.
- **Implementation Quality** — code quality and completeness, **verified via
  the GitHub repo and demo video**. The video is nominally optional and
  effectively required.

### Supporting

- **Problem Statement Alignment** — how well the solution addresses the chosen
  statement.
- **Documentation Quality** — completeness and clarity of the detailed
  description and supporting content.
- **Security** — adequacy of security safeguards, including data protection
  and safe system-level operation.
- **Scalability** — ability to scale across larger datasets, user bases or
  deployment environments.

### Additional

- **Datasets Used** — relevance, quality, compliance.
- **User Experience** — usability, clarity, accessibility.
- **Presentation** — clarity of the PDF and the live Stage 2 presentation.

## Submission rules

- Only registered users may participate.
- Per problem statement, a participant may enter individually **or** as part of
  a team, not both.
- One team may submit to more than one problem statement.
- **Once a problem statement is finalised it cannot be changed.**
- Participants must be Indian citizens enrolled in a recognised institution.
- Team size 1–5.
- All submissions must be original and plagiarism-free.
- Final solution must be submitted before the deadline.
- If no submission qualifies in a track, the Committee may withhold or
  reallocate the prize. Its decision is final.
- C-DAC members from any Centre are ineligible.
- **Linux-based solutions only** — must be developed for and compatible with a
  Linux OS. (We comply trivially.)

---

## Two gaps this rubric exposes

### 1. AI / ML is a **core** criterion and the project currently has none

The form demands a **Model Type** (open-source or inbuilt), and both the Stage 1
core criteria and the Stage 2 jury list include AI depth explicitly. This is
almost certainly a generic form shared across all eight problem statements,
several of which are AI-native — but that does not help us. Leaving it blank
means scoring zero on a core axis.

**Do not bolt on a language model to satisfy a form.** It would be transparent,
it would dilute a submission whose whole credibility rests on not overclaiming,
and a kernel-adjacent jury will see through it instantly.

**The honest fit is adaptive notification, which is already the next work item.**
The spin-then-futex threshold is currently a fixed constant. Learning it online
from the observed inter-arrival distribution — an explore/exploit problem with a
measurable objective (minimise wakeup latency subject to a CPU budget) — is a
genuine adaptive-policy technique, sits exactly where the framework needs it,
and serves the determinism goal rather than decorating it. That is defensible as
an **Inbuilt Model**: in-house, purpose-built, justified.

Frame it as an adaptive/online-learned policy, not as "AI". Overclaiming here
costs more than the criterion is worth.

### 2. Security is a scored criterion — which promotes Layer 3

"Adequacy of security safeguards, including **safe system-level operation**"
is now scored directly. The Layer 3 kernel arbitration layer — preventing a
buggy or malicious peer from corrupting the shared ring, which no pure-userspace
framework including iceoryx can do — now scores on **two** axes at once:
Novelty & Innovation, and Security.

It was already the strongest remaining work item. It is now the clearest.

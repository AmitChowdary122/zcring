# HACKATHON.md — the official rules and requirements

**This file outranks everything else in the repo.** `PLAN.md`, `README.md`,
and `CONTEXT.md` describe how we intend to win; this file describes what we are
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

## Deadline — verified from the portal

**Stage 1 closes ~25 Aug 2026, 00:00 IST** — i.e. midnight at the end of
24 Aug. Confirmed 20 Aug from the portal countdown (98 hours remaining).
Earlier notes in this repo said "~23–24 Aug", inferred from a 400-hour
countdown seen on 7 Aug; that inference was a day pessimistic. **This line is
the authority.** Re-check the countdown before relying on it again.

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

## How the project answers two of these criteria

**AI / Technical Approach.** The submission form requires a **Model Type**
(open-source or inbuilt). The project's answer is adaptive notification: the
spin-then-futex threshold is learned online from the observed inter-arrival
distribution — an explore/exploit problem with a measurable objective
(minimise wakeup latency subject to a CPU budget) — rather than left as a
fixed constant. That's a genuine adaptive-policy technique that serves the
determinism goal directly, and it's what "Inbuilt Model" (in-house,
purpose-built) refers to on the form. It's framed as an adaptive/online-learned
policy, not as "AI" — there is no bolted-on language model here.

**Security.** "Adequacy of security safeguards, including safe system-level
operation" is scored directly. Layer 3 (kernel-enforced arbitration) is the
project's answer: it prevents a buggy or malicious peer from corrupting the
shared ring, which no pure-userspace framework — including iceoryx — can do.

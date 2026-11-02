# AI Background Remover — OFX Plugin for DaVinci Resolve

**Status: PLANNING / AWAITING APPROVAL.** No plugin code has been written yet. Nothing in this
repository has been built or tested in DaVinci Resolve.

This folder is the pre-implementation design package requested in the project brief
("Investigate → Propose architecture → Explain trade-offs → Create implementation plan →
**wait for approval**").

| Doc | What it answers |
|---|---|
| [01-research-and-findings.md](01-research-and-findings.md) | What is actually true about the target environment, the OpenFX SDK, ONNX Runtime execution providers, and the candidate AI models — including licenses. Ends with the **verification register**. |
| [02-architecture.md](02-architecture.md) | The five-layer module design, source tree, data flow, threading and memory model, OFX correctness checklist, and the full inspector parameter specification. |
| [03-tradeoffs-and-decisions.md](03-tradeoffs-and-decisions.md) | Decision records: runtime, model, resolution strategy, temporal stability, build system — each with the alternatives rejected and why. |
| [04-implementation-plan.md](04-implementation-plan.md) | Phased build plan with per-phase acceptance criteria, and an explicit split of **what can be verified in this sandbox** vs. **what only you can verify on Windows/Resolve**. |
| [05-build-install-test-windows.md](05-build-install-test-windows.md) | MSVC + CMake build recipe, `.ofx.bundle` layout, ONNX Runtime acquisition, model acquisition, and the test matrix. |

## The short version

1. **Runtime:** ONNX Runtime, loaded *dynamically at run time* (`LoadLibrary` + `OrtGetApiBase`), so one
   plugin binary works against a CPU-only, DirectML, or CUDA ONNX Runtime and degrades gracefully.
   Staged bring-up in the order **CPU → DirectML → CUDA**, with automatic probing and fallback.
2. **Model:** **BiRefNet_lite (MIT, code *and* weights)** as the quality default, with a small
   portrait/general fallback model for speed and low VRAM. SAM 2, RVM and ViTMatte are rejected as
   primary engines (prompts-required, GPL-3.0, and trimap-required respectively) — details in doc 03.
3. **Quality trick that matters most on 4 GB VRAM:** run inference at a modest matte resolution
   (384–768), then transfer fine detail (hair/fur) back to full resolution with a **guided filter**
   using the original frame as guide. Cheap on CPU, no VRAM cost, and it is what stops a low-res
   matte from looking soft.
4. **Temporal stability:** request OFX sequential rendering (`kOfxImageEffectInstancePropSequentialRender = 2`),
   read `kOfxImageEffectPropSequentialRenderStatus`, and apply a motion-aware EMA on the matte only
   when the host really is rendering in order. Scrubbing stays correct.
5. **Honesty about testing:** the algorithmic layers (pixel conversion, premultiply, matte refinement,
   compositing, caching, temporal) can be genuinely built and unit-tested here on Linux, and the OFX
   plugin layer can be compiled against the real SDK and driven by a headless OFX host harness.
   **MSVC compilation, DirectML/CUDA execution, and DaVinci Resolve behaviour cannot be tested in this
   sandbox.** Those are yours to run, with exact commands provided.

## Open questions blocking sign-off

See [01-research-and-findings.md § 7 — Verification register](01-research-and-findings.md). The three
that could change the plan:

- **V1 — Does your DaVinci Resolve Free 21.1 actually load third-party OFX plugins?** Public sources
  contradict each other. Evidence leans strongly to *yes*, but this is project-blocking and takes you
  two minutes to confirm.
- **V2 — Which ONNX Runtime variant do you want to standardise on?** DirectML is the lowest-friction
  GPU path but is frozen at ORT 1.24.4 and in "sustained engineering"; CUDA is fastest but needs
  cuDNN 9 + CUDA runtime DLLs alongside a 4 GB card.
- **V3 — Plugin identity** (unique ID prefix, displayed name, Resolve Effects grouping).

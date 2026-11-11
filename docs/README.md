# AI Background Remover — OFX Plugin for DaVinci Resolve

**Current status:** U2NetP AI segmentation runs on CPU when the optional model and ONNX Runtime
are installed. It has been tested using real inference on Linux, including a fake OFX host. The prior AI build compiled on Windows and produced renders in Resolve. The new manual guidance
feature has not yet been tested in Windows/Resolve; previously reported playback crashes remain
unproven fixed. The original colour-key fallback remains available.

## Experimental in-Resolve AI box selection

New instances start with the source image unchanged. Drag a loose AI add box to select;
optional **DETAIL ON** tries to recover more foliage, but can retain unwanted background.
See [08-ai-box-selection.md](08-ai-box-selection.md) for setup, controls and limitations.
The new detail recovery and labelled toolbar still require Windows/Resolve validation.

## Experimental in-Resolve viewer brush

A new prototype lets the user paint direct keep/remove corrections in the Resolve viewer, without
GIMP or a PNG. It does not yet perform GIMP-like automatic selection or tracking and has not yet
been tested inside Resolve. See [07-viewer-brush.md](07-viewer-brush.md).

## Painted guidance PNG (optional alternative)

Optional full-frame transparent PNG corrections can force specific pixels to be kept (green) or
removed (red) on top of the AI or colour key. This is manual painting, **not GIMP's matting
algorithm**, and it does not track moving objects. See the [step-by-step guide](06-guidance-mask.md).

## Current controls and log location

For the legacy automatic workflow, choose **Automatic + manual corrections** and **U2NetP (AI, CPU)** under **Automatic Model (legacy)** after installing the AI files and restarting Resolve. AI box selection uses EfficientSAM instead. Existing effects may retain **No AI model** and need to be switched manually. Matte Bias,
invert and output controls apply to AI; Background Colour Tolerance and Softness apply only to the
colour key. Edge refinement, GPU and temporal features are not implemented yet. AI is fixed at
320×320, may be slow on CPU and is not expected to produce fine hair-level matting.

The preferred log is `D:\AI-Effects\ObjectRemoverPlugin-Fix\log.txt` when building from that
checkout. If unavailable, the plugin tries `%ProgramData%\AiBgRemover\log.txt`, then
`%LOCALAPPDATA%\AiBgRemover\log.txt`. Existing projects may show stale static labels. The
installer preserves any old bundle log before replacement. See the [Windows guide](05-build-install-test-windows.md).

## Stability update

The current build no longer writes diagnostic parameters from render or on every setting change;
read-only status and log-path labels are descriptor defaults, not live status displays. It declares
instance-safe rendering, disables host frame threading, and guards reusable scratch against cache
purges and overlapping renders. A fake OFX host tests these paths; it is not a substitute for
Resolve. See the Windows guide's stability retest before using this build on a real project.

## Documents

The planning documents below include historical proposals; the Windows guide and the current
status above take precedence over proposed features and acquisition paths.

| Doc | What it answers |
|---|---|
| [01-research-and-findings.md](01-research-and-findings.md) | What is actually true about the target environment, the OpenFX SDK, ONNX Runtime execution providers, and the candidate AI models — including licenses. Ends with the **verification register** (V1–V14). |
| [02-architecture.md](02-architecture.md) | The five-layer module design, source tree (§1, with §1.1 recording what is *actually built* and where it deviated from the plan), data flow, threading and memory model, OFX correctness checklist, and the full inspector parameter specification. |
| [03-tradeoffs-and-decisions.md](03-tradeoffs-and-decisions.md) | Decision records **D1–D11**: runtime, model, resolution strategy, temporal stability, build system, the raw-C-API choice, and the output-semantics decision that a test caught. Each with the alternatives rejected and why. |
| [04-implementation-plan.md](04-implementation-plan.md) | Phased build plan with per-phase acceptance criteria, an explicit split of **what can be verified in this sandbox** vs. **what only you can verify on Windows/Resolve**, and a status block under each completed phase. |
| [05-build-install-test-windows.md](05-build-install-test-windows.md) | MSVC + CMake build recipe, the `dumpbin` export/dependency checks, `.ofx.bundle` layout, ONNX Runtime acquisition, model acquisition, and the test matrix — including the measured test results. |
| [reference/OpenFX_Resolve_From_Scratch.pdf](reference/OpenFX_Resolve_From_Scratch.pdf) | Your own tested field guide: a third-party raw-C-API OFX plugin loading, exposing a parameter and rendering in Resolve Free 21 on Windows 11. **D10 rests on this document**, and its two Resolve-specific gotchas are honoured in code. |

Also in the repository, outside `docs/`: `packaging/THIRD_PARTY_LICENSES.txt`, which states precisely
which third-party components are redistributed at each phase — including the fact that the NVIDIA
CUDA/cuDNN DLLs the CUDA build needs are **not** open source and are not cleared for redistribution.

## Historical design proposals (not current feature status)

1. **Runtime:** ONNX Runtime, loaded *dynamically at run time* (`LoadLibraryW` + `OrtGetApiBase`), with CPU support implemented. GPU execution providers remain future work.
2. **Model:** **BiRefNet_lite (MIT)** as the quality default, with a small portrait/general model for
   speed and low VRAM. RVM is rejected outright (GPL-3.0, incompatible with a plugin loaded into a
   proprietary host); SAM 2 needs interactive prompts; MODNet's *weights* may be CC BY-NC-SA despite
   Apache-2.0 code, so it is not cleared. Details in doc 03 §D3 and doc 01 §6.
3. **Quality trick that matters most on 4 GB VRAM:** run inference at a modest matte resolution
   (256–1024, default 512), then transfer fine detail (hair/fur) back to full resolution with a
   **guided filter** using the original frame as guide. Cheap, separable, zero VRAM. The resampling
   half is built and tested; the guided filter is Phase 5.
4. **Temporal stability:** request OFX sequential rendering with
   `kOfxImageEffectInstancePropSequentialRender = 2` — written directly as an int, because the SDK's
   `setSequentialRender(bool)` can only express 0 or 1 — read
   `kOfxImageEffectPropSequentialRenderStatus`, and apply a motion-aware EMA on the matte only when the
   host really is rendering in order. The property is set and read; the EMA is Phase 6.
5. **Raw OFX C API, not the SDK's C++ support library** (doc 03 §D10). Your field guide proves that
   route works on the exact target machine; the support library is untested there, throws across the
   host boundary, and drags in a public expat dependency through `HostSupport`.
6. **Honesty about testing:** the algorithmic layers are genuinely built and unit-tested here. The OFX
   layer compiles and its export table is verified with `nm -D`. **MSVC compilation and DirectML/CUDA
   execution cannot be tested in this sandbox.** The user observed a prior build in Resolve; the
   current stability fix remains unverified there. Doc 04 tags every task 🟢 sandbox-verifiable /
   🟡 Windows-only / 🔴 Resolve-only.

## Verification register

The full list is [doc 01 §7](01-research-and-findings.md) — fourteen items, V1 to V14.

**Closed:**

- **V1 — Does Resolve Free 21.1 load third-party OFX plugins?** **Yes.** You confirmed it, and your
  field guide records it working. This was the project-blocking question and it is answered.

**Instrument built, awaiting one run inside Resolve:**

- **V7 — Which OFX API version does Resolve implement, and which host properties does it advertise?**
  `HostBridge::acquireSuites()` reads the same host property list as the SDK's own
  `fetchHostDescription` and writes it to the log on load. Set **Diagnostics → Log Level = Verbose**
  and read the `host:` lines. Every one of those properties is guarded with a fallback regardless, so
  a host that omits one degrades rather than fails.

**Still open, and still your call:**

- **V2 — Which ONNX Runtime variant to standardise on first?** DirectML is the lowest-friction GPU
  path but is frozen at ORT 1.24.4 and in maintenance mode; CUDA is likely fastest on an RTX 2050 but
  needs 1.5–3 GB of cuDNN 9 + CUDA runtime DLLs against a 4 GB card, and those DLLs are **not**
  redistributable under an open-source licence. The plan defaults to CPU → DirectML → CUDA and picks
  the shipping default from a Phase 3 benchmark on your machine.
- **V3 — Plugin identity.** Currently `com.hirdesh1729.aibgremover`, label "AI Background Remover",
  grouping "AI Matting", all overridable at configure time (`-DOFX_PLUGIN_ID_PREFIX=…`). **The unique
  ID is permanent** — a saved Resolve project references the plugin by that string, so changing it
  later orphans existing projects. Worth settling before the first install.
- **V6 — Free disk space.** Your `Get-Volume` output was truncated. Matters because the CUDA path
  alone needs several GB.
- **V9 — Repository name vs. function.** `ObjectRemoverPlugin` implies inpainting; the brief is
  background removal. Naming only, but it shows up in the bundle ID.
- **V10–V13 —** model weight licences, the chosen export's real tensor signature, and measured
  EP × model × resolution latency and VRAM on your card. All Phase 3.

## Building it yourself

Windows, from an **x64 Native Tools Command Prompt for VS 2026**, with Resolve completely closed:

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ^
      -DOPENFX_SDK_DIR="D:/DavinciCoding/OpenFX/openfx"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix build\bundle
```

Then verify the binary before installing it, and follow
[doc 05](05-build-install-test-windows.md) for the install steps and the `dumpbin` checks. Two rules
from your field guide that are easy to forget: run as **administrator** (Program Files), and **close
Resolve completely** first (a loaded `.ofx` is file-locked).

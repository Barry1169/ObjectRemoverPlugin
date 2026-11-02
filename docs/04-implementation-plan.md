# 04 — Implementation Plan

Ten phases. Each has an explicit **definition of done**, and each task is marked with who can verify
it:

- 🟢 **Sandbox-verifiable** — I can build and/or test it here (Linux, g++, no GPU, no Resolve) and
  report real results.
- 🟡 **Windows-only** — compiles/runs only on your machine (MSVC, DirectML, CUDA).
- 🔴 **Resolve-only** — observable only inside DaVinci Resolve Free.

I will not mark anything "done" unless it is 🟢 and actually ran, or unless you report a 🟡/🔴 result
back to me.

---

## Verification split — what this sandbox can and cannot prove

| Layer | Compile here | Unit-test here | Run with real inference here | Run in Resolve |
|---|---|---|---|---|
| `util/`, `core/` pixel & colour math | 🟢 | 🟢 | n/a | — |
| `matte/` refinement & temporal | 🟢 | 🟢 | n/a | — |
| `composite/` | 🟢 | 🟢 | n/a | — |
| `models/` discovery, registry, LRU cache | 🟢 | 🟢 (with a stub session) | — | — |
| `inference/ort/` loader & EP policy | 🟢 | 🟢 **against a stub `onnxruntime` shared library that I write** — proves the symbol resolution, API-table versioning, CUDA/DML probing and fallback chain logic | ❌ no real ORT, no GPU | — |
| `inference/` segmenters (pre/post-processing) | 🟢 | 🟢 (golden tensors captured from a Python reference run **by you**, or hand-computed) | ❌ | — |
| `plugin/` OFX layer | 🟢 compiles against the real SDK on Linux (`.so`) | 🟢 via a **headless OFX host harness** built from the SDK's `HostSupport` | ❌ | — |
| Windows `.ofx` binary, MSVC | ❌ | ❌ | ❌ | 🟡 you |
| DirectML / CUDA execution | ❌ | ❌ | ❌ | 🟡 you |
| Resolve Free inspector, playback, render | ❌ | ❌ | ❌ | 🔴 you |

Two enabling steps make the 🟢 column real:
1. `pip install cmake` (the 4.4.4 wheel is available here — close to your 4.4.3).
2. For the headless host harness only, `expat` dev headers are needed. Not installed here, but
   libexpat is buildable from source fetched from GitHub. If that proves awkward, the harness falls
   back to calling the plugin's exported `OfxGetPlugin`/`OfxSetHost` entry points directly with a
   hand-written minimal host — which needs no expat at all. **The harness is a nice-to-have; the unit
   tests are the load-bearing verification.**

---

## Phase 0 — Environment probe and project scaffolding  🟢🟡

**Goal:** stop guessing about your toolchain.

| # | Task | Verify |
|---|---|---|
| 0.1 | `tools/probe-env.ps1` — dumps `cl.exe` version, `cmake --version`, installed VS/Build Tools, Windows SDK versions, CUDA toolkit presence (`nvcc`), cuDNN DLLs on `PATH`, free disk per volume, Resolve version from the registry, `OFX_PLUGIN_PATH` | 🟡 you run it, paste output → closes **V4, V6** |
| 0.2 | `tools/probe-ofxsdk.ps1` — reports the OFX version of `D:\DavinciCoding\OpenFX\openfx` (grep for `kOfxImageEffectPropThumbnailRender`, `kOfxImageEffectInstancePropSequentialRender`) without modifying it | 🟡 closes **V5** |
| 0.3 | Repo scaffolding: `CMakeLists.txt`, `cmake/*.cmake`, `.gitignore`, `.clang-format`, `docs/` links, empty target tree per arch §1 | 🟢 configures and builds an empty lib here |
| 0.4 | `util/` — `Log`, `Status`, `Hash`, `ScopeTimer`, `Semaphore`, `FixedPool`, path helpers | 🟢 compiles + unit-tested |
| 0.5 | `tests/` harness (a tiny assertion framework — no external dependency, so no Conan/vcpkg/FetchContent needed offline) | 🟢 runs |

**Done when:** the project configures and builds an empty plugin target on Linux; the probe scripts
exist for you to run.

---

## Phase 1 — OFX plugin skeleton that Resolve can see  🟢🟡🔴

**This phase answers V1 and V7, the two things that could invalidate the whole project.**

| # | Task | Verify |
|---|---|---|
| 1.1 | `plugin/OfxEntry.cpp` — `OfxGetPluginIDs`, `OfxGetPlugin`, `OfxSetHost` with correct MSVC `__declspec(dllexport)` (via an `OFX_EXPORT` macro that is `__declspec(dllexport)` on MSVC and `visibility("default")` elsewhere) | 🟢 compiles; 🟡 `dumpbin /exports` shows all three |
| 1.2 | Plugin factory: context `General`, single input clip `Source`, output clip `Output`; declare RGBA + Byte/Short/Half/Float; `setSupportsTiles(false)`; `setSupportsMultiResolution(true)`; request sequential render **value 2 by writing `kOfxImageEffectInstancePropSequentialRender` directly** (the support library's `setSequentialRender(bool)` can only write 0/1 — doc 01 §4.4); `setOverlayInteractDescriptor` stub | 🟢 |
| 1.3 | **Pass-through render**: copy src→dst honouring bit depth, `rowBytes`, bounds, and `renderWindow`; null-src → transparent black; `isIdentity()` returns the source clip when `enable` is false | 🟢 via host harness: synthetic frame in, byte-identical frame out, all four bit depths |
| 1.4 | `HostCompat` diagnostic dump on first load, read straight from `OFX::ImageEffectHostDescription`: `hostName`/`versionLabel`, `APIVersionMajor.Minor`, `sequentialRender`, `supportsStrChoice`, `supportsRenderQualityDraft`, `supportsTiles`, `supportsMultipleClipDepths/PARs`, `supportsCudaRender`/`supportsCudaStream`, `maxParameters`, `maxPages`, `pageRowCount`, `pageColumnCount`, available suites. Also log the raw render in-args each call at Verbose, to see whether Resolve sets `ThumbnailRender`/`NoSpatialAwareness` (not plumbed into `RenderArguments`). | 🟢 logs; 🔴 **you read the log from Resolve** → closes **V7, V14**, and tells us whether Phase 9 is even possible |
| 1.5 | Minimal params: `enable`, `outputMode`, plus the `Diagnostics` tab. Inspector layout adapts to the host's reported `maxPages`/page grid (fall back from Page params to nested Groups if the host cannot lay out six tabs). | 🟢 |
| 1.6 | CMake install rule producing `AiBgRemover.ofx.bundle/Contents/Win64/AiBgRemover.ofx` | 🟢 layout check on Linux; 🟡 on Windows |
| 1.7 | `docs/BUILD-WINDOWS.md` finalised with your actual generator name from 0.1 | 🟡 |

**Done when (🔴 — the gate):** you build with MSVC, copy the bundle to
`C:\Program Files\Common Files\OFX\Plugins`, restart Resolve Free, and
*the plugin appears under Effects → OpenFX, applies to a clip, and passes the image through
unchanged.* If it does **not** appear, V1 is answered "no" and we stop and reassess before writing a
single line of AI code. I will not proceed past this gate on an assumption.

---

## Phase 2 — Image I/O, colour and compositing (no AI yet)  🟢🟡🔴

Prove every pixel-format and colour claim before a model is anywhere near it.

| # | Task | Verify |
|---|---|---|
| 2.1 | `core/PixelConvert` — Byte/UShort/Half/Float ⇄ float32, own IEEE-754 half implementation | 🟢 round-trip tests incl. denormals, ±Inf, NaN, exact 0/1 boundaries |
| 2.2 | `core/Premultiply` — premult ⇄ straight with epsilon guard | 🟢 tests: double-premul detection, zero-alpha safety |
| 2.3 | `core/ColorSpace` — sRGB/Rec709/Linear/Log transforms, applied to scratch only | 🟢 tests: identity round-trip within tolerance; **assert composited RGB is bit-identical to source RGB** |
| 2.4 | `composite/Compositor` — all four output modes + `backgroundTransparency` + `matteInvert` | 🟢 tests driven by a **synthetic analytic matte** (e.g. a soft-edged disc) — no AI needed |
| 2.5 | `OfxImageAdapter` — OFX::Image ⇄ ImageView across stride/bounds/window | 🟢 via harness |
| 2.6 | Wire 2.1–2.5 into `render()` with a **procedural placeholder matte** (a luminance-key or a fixed shape) so the full path is exercised end-to-end | 🟢 harness; 🔴 **you see a working, controllable matte effect in Resolve with real inspector response** |
| 2.7 | Verify no colour shift and no black-background artifacts on: 8-bit, 10-bit, 16-bit, float timelines; Color page and Edit page | 🔴 you, using `tools/checklist-phase2.md` |

**Done when:** in Resolve you can switch all four output modes, move `backgroundTransparency`, and see
correct results with **no colour shift and no unintended opaque black** on any bit depth. Still no AI.

---

## Phase 3 — ONNX Runtime loader, EP policy, model management  🟢🟡

| # | Task | Verify |
|---|---|---|
| 3.1 | `inference/ort/OrtLoader` — `LoadLibraryW`, search order (bundle dir → `%ProgramData%` → `PATH`), `OrtGetApiBase`, version clamping, `GetProcAddress` for the DML export | 🟢 **against a stub `libonnxruntime.so` I write** that returns a controlled API table. Tests: found / not found / too-old version / DML symbol absent / DML symbol present |
| 3.2 | `OrtHandles` RAII for Env / SessionOptions / Session / Value / MemoryInfo / Status | 🟢 leak-checked under ASan with the stub |
| 3.3 | `OrtEpPolicy` — ordered attempt list from the `gpuAcceleration` param; CUDA via `SessionOptionsAppendExecutionProvider_CUDA_V2` with `gpu_mem_limit` + `arena_extend_strategy=kSameAsRequested`; DML via the exported symbol / `GetExecutionProviderApi("DML",…)`; CPU always last | 🟢 decision-table unit tests with the stub; 🟡 **real EP selection on your machine** |
| 3.4 | `models/ModelDiscovery` — search paths, `manifest.json` parse, SHA-256 verify, list available models for the `model` choice param at describe time | 🟢 tests with fixture dirs |
| 3.5 | `models/ModelRegistry` — process-wide refcounted session cache, per-key init mutex, idle eviction, thread-safe | 🟢 concurrency test: 8 threads × 1000 acquire/release, ASan/TSan clean, exactly one session created |
| 3.6 | `models/MatteCache` — LRU with byte budget, key = arch §3 4d | 🟢 tests incl. eviction order and budget enforcement |
| 3.7 | Global inference semaphore + "Max Concurrent Inferences" | 🟢 tests |
| 3.8 | `tools/fetch_models.py` — downloads the chosen model(s) with pinned SHA-256, writes `manifest.json`. **You run this once; the plugin is offline afterwards.** | 🟡 you run it (HF is not reachable from here) |
| 3.9 | `tools/probe_model.py` — prints a model's real input/output names, shapes, dtypes; runs a round-trip at 384/512/1024; reports peak RSS | 🟡 closes **V12**; also screens out any export with a 10 GB memory spike (doc 01 §6.2) |
| 3.10 | `tools/bench/` — standalone exe reusing `inference/` + `models/`: EP × model × resolution → p50/p95 latency, peak process memory, and (CUDA) VRAM from `nvidia-smi` sampling | 🟡 closes **V13**, and settles **V2** with data instead of opinion |

**Done when:** 🟢 the loader/policy/registry/cache are unit-tested and leak-clean here; 🟡 `bench.exe`
on your machine produces a real table of latency and memory for CPU vs DML vs CUDA, and we pick the
default from it.

---

## Phase 4 — First real segmentation  🟢🟡🔴

| # | Task | Verify |
|---|---|---|
| 4.1 | `inference/Preprocess` — aspect-preserving resize to matte res, pad to model multiple, normalise (mean/std), pack NCHW | 🟢 golden-value tests against a Python reference you run once |
| 4.2 | `BiRefNetSegmenter` — run, read output, sigmoid if needed, unpad, return raw alpha | 🟢 with recorded golden tensors; 🟡 live |
| 4.3 | `matte/Resize` — area-decimate down, bicubic up | 🟢 tests: DC preservation, no ringing overshoot beyond tolerance |
| 4.4 | Wire into `Pipeline` replacing the Phase 2 placeholder matte | 🟢 harness with a mock segmenter; 🔴 **first real background removal in Resolve** |
| 4.5 | Error/absence paths exercised for real: no ORT DLL, no model file, wrong SHA, EP unavailable, OOM | 🟢 stub-driven; 🟡 you delete the model and confirm pass-through + status message, **no crash** |

**Done when:** 🔴 a portrait in Resolve Free gets its background removed. Also: with the model file
deleted, the node passes through cleanly and the Diagnostics status explains why.

---

## Phase 5 — Matte refinement  🟢🟡🔴

| # | Task | Verify |
|---|---|---|
| 5.1 | `matte/GuidedFilter` (separable box-filter implementation, radius auto-derived) | 🟢 tests: constant guide → identity; edge preservation measured against a synthetic hair-like pattern |
| 5.2 | `matte/Morphology` — sub-pixel erode/dilate via a distance transform | 🟢 tests: known-radius circular structuring element; monotonic in the parameter |
| 5.3 | `matte/Feather` — separable Gaussian on the transition band | 🟢 tests: total alpha mass changes smoothly, no clipping |
| 5.4 | `matte/Smooth` — edge-aware smoothing | 🟢 tests |
| 5.5 | `matte/Components` — connected components, keep-largest, despeckle | 🟢 tests on synthetic multi-blob mattes |
| 5.6 | `MatteRefiner` facade with the exact arch §3 4g ordering; `shiftMatte` | 🟢 |
| 5.7 | Param-change invalidation rules (arch §8) | 🟢 tests; 🔴 scrubbing a refinement slider feels instant (raw-matte cache hit) |

**Done when:** all five refinement controls respond correctly in Resolve on a **detailed-hair
portrait**, and refinement slider changes are instant while model/quality changes cost one inference.

---

## Phase 6 — Temporal stability  🟢🟡🔴

| # | Task | Verify |
|---|---|---|
| 6.1 | `matte/Temporal` — state struct, validity rules (arch §6), motion measure, adaptive EMA | 🟢 tests: static sequence → variance reduction; scene cut → no bleed across the cut; reverse scrub → state resets |
| 6.2 | Wire `SequentialRenderStatus` from render args; `resetTemporal` button; frame-period derivation from `Clip::getFrameRate()` with tolerance compare | 🟢 harness drives in-order and out-of-order sequences |
| 6.3 | Measure flicker: render a synthetic moving-subject sequence with strength 0 vs 0.35 vs 0.7 and report per-pixel temporal variance | 🟢 quantified here on synthetic data |
| 6.4 | Real clip test | 🔴 you, on a moving-subject video clip |

**Done when:** 🟢 measured temporal variance drops with strength on synthetic sequences **and**
out-of-order rendering produces identical results to temporal-disabled (proving correctness);
🔴 you confirm reduced flicker on a real clip without objectionable ghosting.

---

## Phase 7 — Performance and VRAM budget  🟡🔴

| # | Task | Verify |
|---|---|---|
| 7.1 | `processingResolution` (follow-host / force half / quarter / full) | 🟢 tests; 🔴 proxy playback |
| 7.2 | `renderQualityDraft` → auto-downgrade; `thumbnailRender` → skip model entirely | 🟢 harness asserts the segmenter is never called for a thumbnail |
| 7.3 | Zero-allocation audit of the steady-state path (grow-only buffers; no per-frame `new`) | 🟢 allocation counter in tests: **0 allocations after warm-up** |
| 7.4 | CUDA `gpu_mem_limit` + `arena_extend_strategy` wiring; idle session eviction to return VRAM | 🟡 measured with `nvidia-smi` during a Resolve session |
| 7.5 | `preloadModel`, `clearCache` | 🟡🔴 |
| 7.6 | Multi-instance behaviour: two nodes using the plugin simultaneously | 🟡🔴 — confirms the registry refcounting under a real host |
| 7.7 | Optional: ViTMatte stage-2 (`detailRefinement` high) | 🟢🟡 — only if the Phase 3 benchmark shows VRAM headroom |

**Done when:** 🟢 zero steady-state allocations; 🟡 peak VRAM under the configured limit with Resolve
running, and no OOM at 1080p and 4K; 🔴 playback is usable at proxy scale.

---

## Phase 8 — Polish, optional extras  🟢🟡🔴

| # | Task | Verify |
|---|---|---|
| 8.1 | Overlay interact: click in the viewer to pick which subject to keep (drives `Components`) | 🔴 |
| 8.2 | `dumpMatte` PNG export; log rotation | 🟢🟡 |
| 8.3 | Presets (Portrait / Product / Animal / Green-Screen-Free / Fast Preview) | 🔴 |
| 8.4 | Installer (Inno Setup) + `THIRD_PARTY_LICENSES.txt` (OFX BSD-3-Clause, ORT MIT, model MIT/Apache with attribution) | 🟡 |
| 8.5 | Optional: click-to-pick via SAM 2 — **only if you ask for it**; it is a separate model and a separate prompt UX | — |

---

## Phase 9 — Deferred optimisation (explicitly out of v1 scope)

Zero-copy interop between Resolve's GPU frame and ORT's CUDA tensors via the OFX GPU render suite,
removing two full-frame PCIe transfers per frame. Depends on **V7** showing Resolve Free exposes the
CUDA render suite to third-party plugins, which I have not verified and will not assume. See D8.

---

## Recommended build order and effort

Phases 0–2 are 🟢-heavy: I can do nearly all of it here and show you real test output.
Phase 1's 🔴 gate is the first thing that needs you, and it is the cheapest possible test of the
riskiest assumption (V1). So:

```
Phase 0 → Phase 1 (build → YOU TEST IN RESOLVE ← gate) → Phase 2 → Phase 3 → ...
                    ↑ stop here if the plugin doesn't load
```

I would rather hand you a pass-through `.ofx` to validate V1 after a small amount of work than build
the entire AI pipeline first and discover Resolve Free will not load it.

---

## Test data plan (your brief §9)

`tools/golden/` will hold, per case: the source frame(s), the expected properties, and the check.

| Case | Source | Check |
|---|---|---|
| Portrait, detailed hair | you supply, or a permissively-licensed stock frame | alpha gradient present in the hair band (not binary); ≥N% of pixels in 0.05–0.95 |
| Person vs complex background | as above | no background leakage above threshold; subject interior alpha ≈ 1 |
| Animal with fur | as above | same as hair |
| Complex outline (e.g. bicycle, foliage) | as above | IoU vs a reference matte |
| Multiple subjects | as above | `Largest Subject Only` keeps exactly one component; `All Subjects` keeps all |
| Video, moving subject | short clip | temporal variance with stabiliser on vs off; no ghosting past a scene cut |
| Resolutions / aspect ratios | 720p, 1080p, 4K, 16:9, 2.39:1, 9:16, 1:1 | matte resolution independence; RoD and PAR handled |
| Bit depths | 8/10/16-bit and float | no banding introduced; RGB bit-identical in Composite mode |
| Model unavailable | delete the model file | pass-through, status message, no crash, Resolve does not disable the plugin |

Synthetic cases (analytic discs, generated hair patterns, moving blobs) run 🟢 here and give real
numbers. The photographic cases need your media or your eyes — I will not claim they pass.

Memory/leak checking runs 🟢 under AddressSanitizer and (for the registry/cache concurrency tests)
ThreadSanitizer on Linux, plus 🟡 Application Verifier / CRT debug heap and `_CrtDumpMemoryLeaks` on
Windows.

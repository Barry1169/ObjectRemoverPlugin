# 02 — Architecture

Five layers, as the brief requires, with a strict dependency rule: **arrows only point downwards.**

```
┌─────────────────────────────────────────────────────────────────────────┐
│ L1  OFX PLUGIN LAYER        plugin/                                     │
│     Resolve integration, inspector params, render actions, host quirks  │
│     The ONLY layer that may #include <ofxs*.h>                          │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │ FrameContext, ImageView, RenderRequest
┌───────────────────────────────────▼─────────────────────────────────────┐
│ L0  PIPELINE (host-agnostic core)   core/                               │
│     Orchestrates the frame. No OFX, no ONNX. Fully unit-testable.       │
└──┬───────────────────┬──────────────────────┬──────────────────────┬────┘
   │                   │                      │                      │
┌──▼────────────┐ ┌────▼─────────────┐ ┌──────▼──────────┐ ┌─────────▼────┐
│ L2 INFERENCE  │ │ L3 MATTE PROCESS │ │ L4 COMPOSITING  │ │ L5 MODEL MGMT│
│ inference/    │ │ matte/           │ │ composite/      │ │ models/      │
│ ORT loader,   │ │ guided filter,   │ │ fg-only,        │ │ discovery,   │
│ EP policy,    │ │ morphology,      │ │ bg-only, matte, │ │ registry,    │
│ segmenters    │ │ feather, smooth, │ │ composite,      │ │ refcounted   │
│               │ │ components,      │ │ bg transparency │ │ sessions,    │
│               │ │ temporal EMA     │ │                 │ │ LRU cache    │
└───────────────┘ └──────────────────┘ └─────────────────┘ └──────────────┘
```

`L2` depends on `L5` (it asks the registry for a session). Nothing depends on `L1`. `core/`,
`matte/`, `composite/`, `models/` and `util/` compile and unit-test on Linux with no OFX and no
ONNX Runtime present — which is what makes honest verification possible in this sandbox.

---

## 1. Source tree

```
ObjectRemoverPlugin/
├── CMakeLists.txt                     # top level: options, targets, install
├── cmake/
│   ├── OpenFXSDK.cmake                # locate SDK via OPENFX_SDK_DIR; build OfxSupport static lib
│   ├── OnnxRuntime.cmake              # optional link-time headers ONLY (we load at runtime)
│   └── BundleLayout.cmake             # assemble <Name>.ofx.bundle/Contents/Win64 + deps
├── plugin/                            # ── L1 ────────────────────────────────
│   ├── OfxEntry.cpp                   #   OfxGetPluginIDs / OfxGetPlugin / OfxSetHost (dllexport)
│   ├── BgRemoverPlugin.h/.cpp         #   describe(), describeInContext(), render(), isIdentity(),
│   │                                  #   getRegionOfDefinition(), getClipPreferences(),
│   │                                  #   beginContextRender/endContextRender, changedParam
│   ├── BgRemoverParams.h/.cpp         #   param IDs, ranges, defaults, group/page construction,
│   │                                  #   param → PipelineSettings mapping
│   ├── OfxImageAdapter.h/.cpp         #   OFX::Image ⇄ core::ImageView (all 4 bit depths,
│   │                                  #   rowBytes stride, bounds vs RoD, render window)
│   ├── HostCompat.h/.cpp              #   host capability probing + Resolve-specific guards
│   └── OverlayInteract.cpp            #   (Phase 8, optional) click-to-pick-subject
├── core/                              # ── L0 ────────────────────────────────
│   ├── Types.h                        #   PixelFormat, ComponentSet, Rect, Size, Time
│   ├── ImageView.h                    #   non-owning strided view over any bit depth
│   ├── FloatImage.h/.cpp              #   RGBA float32 working buffer, grow-only reuse
│   ├── PixelConvert.h/.cpp            #   Byte/UShort/Half/Float ⇄ float32 (own half impl)
│   ├── Premultiply.h/.cpp             #   premult ⇄ straight, epsilon-safe unpremultiply
│   ├── ColorSpace.h/.cpp              #   sRGB/Rec709/Linear/LogC-ish transfer for INFERENCE
│   │                                  #   INPUT ONLY — never touches composited output
│   ├── FrameContext.h                 #   time, renderScale, RoD, bounds, PAR, field, draft,
│   │                                  #   thumbnail, interactive, sequentialRenderStatus
│   ├── PipelineSettings.h             #   plain struct mirroring every inspector control
│   └── Pipeline.h/.cpp                #   the per-frame orchestrator (see §3)
├── inference/                         # ── L2 ────────────────────────────────
│   ├── ort/
│   │   ├── OrtLoader.h/.cpp           #   LoadLibraryW + OrtGetApiBase + GetApi, DML symbol probe
│   │   ├── OrtTypes.h                 #   minimal mirror of the C API structs we touch
│   │   ├── OrtHandles.h               #   RAII: Env, SessionOptions, Session, Value, Status
│   │   ├── OrtEpPolicy.h/.cpp         #   Auto/CUDA/DML/CPU decision + ordered fallback
│   │   └── OrtRunner.h/.cpp           #   Run() with preallocated I/O tensors, error → status
│   ├── ModelSpec.h/.cpp               #   input HW, norm mean/std, output index, sigmoid?,
│   │                                  #   op-variant tag (std vs native DeformConv), dtype
│   ├── Segmenter.h                    #   interface: segment(const FloatImage&) -> Matte
│   ├── BiRefNetSegmenter.h/.cpp
│   ├── SmallMatteSegmenter.h/.cpp     #   MODNet / PP-Matting class
│   ├── ViTMatteRefiner.h/.cpp         #   optional stage 2 (Phase 7)
│   └── Preprocess.h/.cpp              #   letterbox resize + normalize + NCHW pack
├── matte/                             # ── L3 ────────────────────────────────
│   ├── Matte.h                        #   single-channel float32 + dims, grow-only reuse
│   ├── Resize.h/.cpp                  #   area-decimate down, bicubic/bilinear up
│   ├── GuidedFilter.h/.cpp            #   detail transfer using full-res frame as guide  ★
│   ├── Morphology.h/.cpp              #   erode/dilate = Edge Expansion; open/close
│   ├── Feather.h/.cpp                 #   separable Gaussian + distance-based soft edge
│   ├── Smooth.h/.cpp                  #   edge-aware smoothing = Foreground Smoothing
│   ├── Components.h/.cpp              #   connected components, keep-largest, despeckle
│   ├── Temporal.h/.cpp                #   motion-aware EMA + state validity rules  ★
│   └── MatteRefiner.h/.cpp            #   facade applying the inspector controls in order
├── composite/                         # ── L4 ────────────────────────────────
│   ├── OutputMode.h
│   └── Compositor.h/.cpp              #   4 modes + background transparency + matte preview
├── models/                            # ── L5 ────────────────────────────────
│   ├── ModelId.h                      #   enum + string mapping
│   ├── ModelDiscovery.h/.cpp          #   search paths, manifest.json, SHA-256 verify
│   ├── ModelRegistry.h/.cpp           #   process-wide, refcounted, thread-safe session cache,
│   │                                  #   idle VRAM release
│   └── MatteCache.h/.cpp              #   LRU raw-matte cache with a hard memory budget
├── util/
│   ├── Log.h/.cpp                     #   OFX log when available, rotating file log otherwise
│   ├── Status.h                       #   Result<T>/error codes — no exceptions across L1
│   ├── Hash.h, ScopeTimer.h, FixedPool.h, Semaphore.h
│   └── win/Paths.h/.cpp               #   bundle dir, %ProgramData%, DLL directory handling
├── tools/
│   ├── probe-env.ps1                  #   V4: dumps your real toolchain versions
│   ├── probe_model.py                 #   V12: prints a model's true tensor signature
│   ├── fetch_models.py                #   one-time online download + SHA-256 verify
│   ├── export_onnx/                   #   reproducible BiRefNet/MODNet export scripts
│   ├── bench/                         #   standalone exe: EP × model × resolution latency/VRAM
│   └── hostharness/                   #   headless OFX host (SDK HostSupport) for CI-style tests
├── tests/                             #   runs on Linux AND Windows, no Resolve, no GPU required
├── packaging/                         #   Inno Setup script, THIRD_PARTY_LICENSES.md, NOTICE
└── docs/
```

Nothing here is a "one large C++ source file"; the largest single translation unit is expected to be
`BgRemoverPlugin.cpp` at well under 800 lines.

---

## 2. Identity (needs your sign-off — V3)

| Field | Proposal |
|---|---|
| Plugin unique ID | `net.<yourdomain>.ofx.aibgremover` — I need a prefix from you |
| Displayed name | `AI Background Remover` |
| Grouping (Resolve **Effects → OpenFX → …**) | `AI Matting` |
| Bundle folder | `AiBgRemover.ofx.bundle` |
| Output file | `AiBgRemover.ofx` |
| Version | 1.0.0 |
| Plugin description | "Automatic AI subject isolation and background removal." |

OFX IDs are permanent: a saved Resolve project references the plugin by unique ID, so renaming later
breaks old projects. Hence the ask.

---

## 3. Per-frame data flow

```
L1 render(args)
 │
 ├─ 1. dst = dstClip->fetchImage(t);  src = srcClip->fetchImage(t)
 │     • dst null                → return kOfxStatErrMemory (host may retry after purge)
 │     • src null                → fill dst with (0,0,0,0), return kOfxStatOK   ← never black-opaque
 │     • depth/component mismatch with our declared support → pass-through src→dst, log, kOfxStatOK
 │
 ├─ 2. Build core::FrameContext
 │     time, renderScale, renderWindow, RoD, bounds, rowBytes, PAR, fieldToRender,
 │     sequentialRenderStatus, interactiveRenderStatus, renderQualityDraft, thumbnailRender
 │
 ├─ 3. Fast outs
 │     • !enable                  → isIdentity() already redirected; belt-and-braces copy here
 │     • thumbnailRender          → plain copy, MODEL IS NEVER TOUCHED
 │     • draft && quality>Low     → downgrade quality for this call only
 │
 ├─ 4. core::Pipeline::process(ctx, srcView, dstView, settings)
 │     │
 │     ├─ 4a. Convert src → FloatImage (RGBA float32), honouring rowBytes stride and bounds.
 │     │      Grow-only buffer, reused across frames — zero steady-state allocation.
 │     │
 │     ├─ 4b. Premultiplication normalisation.
 │     │      If input declares premultiplied → un-premultiply into a straight-RGBA copy for
 │     │      inference and for compositing; remember to re-apply on output.
 │     │
 │     ├─ 4c. Inference-input colour transform (ColorSpace).
 │     │      Applied to a SCRATCH copy that only the network sees. The composited output is
 │     │      derived from the untouched original, so RGB is bit-exact. ← "no colour shift"
 │     │
 │     ├─ 4d. MatteCache lookup
 │     │      key = hash(clipIdentity, time, renderScale, modelId, matteRes, colorSpaceAssumption)
 │     │      hit → skip 4e/4f entirely
 │     │
 │     ├─ 4e. Downscale to matte resolution (area-decimate; aspect-preserved; padded to the
 │     │      model's required multiple) → Preprocess (normalise + NCHW pack)
 │     │
 │     ├─ 4f. ModelRegistry::acquire(modelId) → Segmenter::segment()
 │     │      • inference guarded by a global counting semaphore (default permits = 1)
 │     │      • on OOM/EP failure → release session, step down the EP chain, retry once,
 │     │        then auto-degrade matte resolution, then pass-through
 │     │      → RAW alpha at matte resolution  (this is what gets cached)
 │     │
 │     ├─ 4g. MatteRefiner (order matters):
 │     │      1. Components      → keep-largest / despeckle (Processing Mode)
 │     │      2. GuidedFilter    → transfer hair/fur detail from the FULL-RES guide  ★
 │     │      3. Morphology      → Edge Expansion (erode/dilate, sub-pixel via distance field)
 │     │      4. Feather         → Edge Feather (separable Gaussian, radius in source pixels)
 │     │      5. Smooth          → Foreground Smoothing (edge-aware)
 │     │      6. (opt) ViTMatte  → Detail Refinement = high, if model present
 │     │      7. Upscale refined matte to full resolution
 │     │
 │     ├─ 4h. Temporal stabiliser  ★
 │     │      Valid only if: temporal enabled AND ctx.sequentialRenderStatus == 1
 │     │                     AND |t − tPrev| == 1 frame AND matteRes/model unchanged
 │     │                     AND no refinement param changed since tPrev
 │     │      Then: motion = mean|I_t − I_{t−1}| within the transition band
 │     │            w     = clamp(strength · (1 − motion/motionRef), 0, maxWeight)
 │     │            α_t   = (1−w)·α_t + w·α_{t−1}          (motion-adaptive EMA)
 │     │      Otherwise: reset state (correct single-frame result, no ghosting).
 │     │
 │     ├─ 4i. MatteCache store (raw matte only; refinement is cheap and re-runs live)
 │     │
 │     └─ 4j. Compositor(outputMode, backgroundTransparency, α, originalRGB) → FloatImage out
 │
 ├─ 5. Re-apply premultiplication as declared on the output clip.
 │
 └─ 6. Write to dst for exactly renderWindow ∩ dst.bounds, converting float32 → dst bit depth.
       Pixels outside src bounds get (0,0,0,0).
```

★ = the two pieces of engineering that most determine perceived quality. §5 and §6 cover them.

---

## 4. Threading, lifetime and memory

### 4.1 What the host may do to us

Resolve may (a) render several frames concurrently on different threads, (b) create several
instances of the same plugin (one per node), (c) call `render` for a thumbnail or a draft pass,
(d) scrub backwards, and (e) tear down instances at any time. The OFX multi-thread suite is
available but we must not assume the host cooperates beyond the documented properties.

### 4.2 Concurrency design

| Resource | Scope | Guard | Notes |
|---|---|---|---|
| `OrtEnv` | process | created once, `std::call_once` | ORT requires one env per process; destroying it while sessions live is fatal |
| `OrtSession` per `ModelId` | process | `ModelRegistry`, `std::shared_mutex` + per-key init mutex | Refcounted. **Idle-evicted after N s** (default 30) to return VRAM to Resolve. |
| Inference execution | process | global counting semaphore, default **1** permit | ORT `Run()` is thread-safe, but N concurrent runs on a 4 GB card = OOM. Exposed as "Max Concurrent Inferences". |
| `MatteCache` | process | `std::mutex`, LRU, byte budget (default 512 MB) | Holds **only our own buffers** — never an `OFX::Image*`, which is invalid after `render` returns |
| Temporal state | per instance | instance `std::mutex` | Reset on `beginContextRender`, on any param change, on model/resolution change, on non-sequential render |
| Scratch buffers | per instance (and per thread where useful) | none needed if per-instance; `thread_local` pool if shared | Grow-only; never shrink during a session |

Rule: **no allocation in the steady-state render path.** All buffers are grow-only and reused; the
only per-frame allocation permitted is inside ONNX Runtime itself.

### 4.3 Error and absence policy

| Condition | Behaviour |
|---|---|
| `onnxruntime.dll` not found | Pass-through, log once, status param says "ONNX Runtime not found — see install guide". Plugin still loads and still appears in Resolve. |
| Model file missing / SHA mismatch | Same as above, naming the missing file. |
| Requested EP unavailable | Step down the chain: requested → CUDA → DML → CPU → give up (pass-through). |
| Session creation fails | Retry once on CPU; then pass-through. |
| CUDA OOM mid-run | Release session, evict cache, drop matte resolution one step, retry once; then pass-through. |
| `fetchImage` returns null on dst | Return `kOfxStatErrMemory` (spec-permitted; host may purge and retry). |
| Any unexpected internal fault | Catch at the L1 boundary — **never let a C++ exception cross into Resolve** — log, pass-through, return `kOfxStatOK`. A visible wrong frame beats a crashed Resolve. |
| `Fail Loudly If Model Missing` (debug bool, default **off**) | Return `kOfxStatFailed` + `errorMessage()` so you can see it during development. |

---

## 5. Why the guided filter is the key quality lever

Running BiRefNet_lite at 1024² is the "obvious" way to get hair detail, and it is exactly what a
4 GB card cannot afford alongside Resolve. Running at 512² and bilinearly upscaling gives a soft,
plastic edge.

A **guided filter** (He et al.) takes the low-res AI matte as *input* and the full-resolution frame
as *guide*, and produces a full-resolution matte whose edges snap to the real luminance/colour
gradients of the original image. Hair strands and fur that exist in the guide but were blurred away
by downscaling are recovered. It is O(N) with a small constant, separable via box filters, runs
comfortably on 6 CPU cores, and costs **zero VRAM**.

Exposed as **Detail Refinement** (0–100, default 60): 0 = plain bicubic upscale, 100 = maximum
detail transfer. Radius auto-derives from `srcHeight / matteHeight`. This is the difference between
"looks like a mobile app" and "looks like a rotoscope".

---

## 6. Temporal consistency — the exact rule set

Requested from the host: `kOfxImageEffectInstancePropSequentialRender = 2`
("best to render sequentially, but still correct if not"). Value `1` would force Resolve to serialise
our node and wreck playback; value `2` still gets us the `kOfxImageEffectPropSequentialRenderStatus`
flag in every render call, which is all we need.

⚠️ Implementation detail: the support library's `OFX::ImageEffect::setSequentialRender(bool)` writes
`int(v)` to that property, so it can only produce **0 or 1**. We must write **2** directly through the
protected `_effectProps` from our subclass (verified in
`Support/Library/ofxsImageEffect.cpp:1314`). Calling the convenience setter would silently request
full serialisation.

Additionally, `OFX::ImageEffectHostDescription::sequentialRender` tells us at load time whether the
host can ever guarantee sequential rendering (`0` = never, `1` = always, `2` = sometimes). If Resolve
reports `0`, temporal stabilisation is **disabled** rather than silently misbehaving, and the
Diagnostics status says so.

State: `{ lastTime, lastMatte, lastFrameThumb, paramsHash, valid }` per instance.

```
useTemporal = settings.temporalStrength > 0
              && ctx.sequentialRenderStatus == 1
              && state.valid
              && frameDelta(ctx.time, state.lastTime) == +1 frame
              && ctx.renderScale == state.renderScale
              && settings.hash()   == state.paramsHash
```

- If `useTemporal` → motion-adaptive EMA (§3, step 4h). The weight collapses towards 0 where the
  image changed a lot, so fast motion does not smear, and stays high in static regions, so flicker
  dies. This is deliberately *not* optical flow: flow would cost more than the segmentation itself.
- If not → **reset** `state.valid = false` and produce the plain per-frame result. Scrubbing, random
  access, and out-of-order multithreaded renders all stay pixel-correct; they just don't get the
  stabilisation. This is the honest interpretation of OFX value `2`.
- `Reset Temporal State` push-button for when you scrub and want the EMA re-primed.
- The frame period is derived from the clip's frame rate (`Clip::getFrameRate()`), with a fallback to
  1.0 and a tolerance-based comparison rather than exact float equality.

**Deliberately not done:** RVM-style recurrent inference. It would be the highest-quality temporal
answer, but RVM is GPL-3.0 and no permissively-licensed recurrent matting model of comparable
quality was found. Revisit only if you accept GPL or we train/derive something ourselves.

---

## 7. Pixel-format and colour handling

| Concern | Approach |
|---|---|
| Bit depths declared | `Byte`, `Short`, `Half`, `Float` for RGBA. All four implemented via a single templated conversion; `Half` uses our own IEEE-754 half ⇄ float (no dependency on `ofxPixels.h` internals beyond the enum). |
| Components | Input `RGBA` (and `RGB` treated as alpha=1). Output `RGBA`. Matte-only mode still writes RGBA (grey in RGB, alpha=1) so downstream Resolve nodes behave normally. |
| Working format | float32 RGBA, always. One conversion in, one out. |
| Premultiplication | Read from the input clip; declare on the output clip; un-premultiply for inference and compositing; re-premultiply on write. Epsilon-guarded so a zero-alpha pixel never divides by zero. |
| Colour space | The AI sees a normalised sRGB-ish image. `Input Color Space` = Auto / Rec.709-sRGB / DaVinci YRGB / Linear / Log. The transform is applied **only to the inference scratch buffer**. Composited RGB comes from the original untouched pixels → **no colour shift is possible by construction.** |
| Region of definition | Identity: output RoD = input RoD. `getRegionOfDefinition` just forwards. |
| Render window | Always honoured on write. Tiles disabled (`setSupportsTiles(false)`) because segmentation needs whole-frame context; Resolve does not tile this class of effect. |
| Render scale | Folded into the matte cache key and into the temporal validity check. Proxy playback at 1/4 scale produces a different (valid) matte than a full-res render. |
| Out-of-bounds | `(0,0,0,0)` — transparent black, never opaque black. This is the "no unintended black backgrounds" requirement. |

---

## 8. Inspector specification

A **Page param** gives tabbed groups in the Resolve inspector. Four tabs matching your brief's
**Background Removal / Matte Refinement / Output / Performance**, plus two additions — **Temporal
(Video)** and **Diagnostics** — which are flagged as deliberate deviations and justified in
[03-tradeoffs-and-decisions.md §D7](03-tradeoffs-and-decisions.md).

Host limits are respected rather than assumed: `OFX::ImageEffectHostDescription` reports
`maxPages`, `pageRowCount`, `pageColumnCount` and `maxParameters` (doc 01 §4.4). If Resolve's page
grid cannot hold six tabs, the layout falls back to nested **Group** params inside a single page. The
parameter *set* never changes — only its presentation.

### Tab 1 — Background Removal

| Param | OFX type | Range / choices | Default | Description |
|---|---|---|---|---|
| `enable` | Boolean | — | **true** | Auto Remove Background. When off the node is an identity (Resolve skips it entirely). |
| `processingMode` | Choice | `Automatic` / `Largest Subject Only` / `All Subjects` / `Invert (Remove Subject)` | `Automatic` | What to do with the detected foreground. `Largest Subject Only` keeps the biggest connected component — the fix for multi-subject shots. `Invert` removes the subject instead of the background. |
| `quality` | Choice | `Draft (256)` / `Fast (384)` / `Balanced (512)` / `High (768)` / `Maximum (1024)` | **`Balanced (512)`** | Segmentation Quality = the AI's working resolution. Directly trades VRAM/time against edge accuracy. |
| `model` | **StrChoice** if the host reports `supportsStrChoice`, else a fixed `Choice` | built at describe time from the models actually found on disk; always ends with `Custom…` | first available | Model Selection. If no model is found the choice contains only `None (pass-through)` and the node degrades gracefully. **Why StrChoice:** a classic index `Choice` bakes its option list at describe time and animates by index, which is wrong for a list derived from files on disk — install a new model and every index shifts. StrChoice carries string values and survives that. |
| `inputColorSpace` | Choice | `Auto` / `Rec.709 (sRGB)` / `DaVinci YRGB` / `Scene Linear` / `Log` | `Auto` | What the network should assume about the incoming pixels. Affects inference only. |
| `despeckle` | Integer | 0–1000 px | 8 | Removes tiny spurious matte islands before refinement. |

### Tab 2 — Matte Refinement

| Param | OFX type | Range | Default | Description |
|---|---|---|---|---|
| `detailRefinement` | Double | 0–100 | **60** | Refine Edges. Guided-filter detail transfer strength from the full-res guide. The hair/fur control. |
| `edgeFeather` | Double | 0–100 px | **1.0** | Edge Feather. Softness of the transition band. |
| `edgeExpansion` | Double | −100 … +100 px | **0** | Edge Expansion. Negative chokes the matte inwards, positive grows it outwards. Sub-pixel accurate. |
| `edgeSmoothing` | Double | 0–100 | **20** | Foreground Smoothing. Edge-aware smoothing of the matte boundary; reduces jaggies without eroding hair. |
| `shiftMatte` | Double | 0–1 (mapped to −0.5…+0.5) | 0.5 | Global matte threshold bias — recovers a subject the model clipped, or kills a fringe. |
| `resetRefine` | PushButton | — | — | Restores the four controls above to defaults. |

### Tab 3 — Output

Your brief lists four booleans here, but four independent booleans can be simultaneously true, which
is meaningless. Implemented as **one Choice** covering exactly the same four states, plus the
transparency control:

| Param | OFX type | Range / choices | Default | Description |
|---|---|---|---|---|
| `outputMode` | Choice | `Composite` / `Foreground Only` / `Background Only` / `Matte Only` | **`Composite`** | Composite = original subject over transparency (alpha written). Foreground Only = subject pre-multiplied, background zeroed. Background Only = the removed background, with the subject hole filled by `backgroundTransparency`. Matte Only = the alpha as a greyscale image. |
| `backgroundTransparency` | Double | 0–1 | **0** | Background Transparency. In `Composite`/`Background Only`, how much of the original background remains visible behind/around the subject. 0 = fully removed. |
| `matteInvert` | Boolean | — | false | Show the inverse matte in `Matte Only`. |
| `premultiplyOutput` | Choice | `Follow Host` / `Premultiplied` / `Straight` | `Follow Host` | Escape hatch for hosts/pages that disagree about premultiplication. Fixes dark halos without a rebuild. |

### Tab 4 — Performance

| Param | OFX type | Range / choices | Default | Description |
|---|---|---|---|---|
| `processingResolution` | Choice | `Follow Host Render Scale` / `Force Half` / `Force Quarter` / `Force Full` | `Follow Host Render Scale` | Processing Resolution. Controls whether the AI pass honours Resolve's proxy render scale or always runs at the selected quality. |
| `gpuAcceleration` | Choice | `Auto` / `CUDA` / `DirectML` / `CPU` / `Disabled` | **`Auto`** | `Auto` probes CUDA → DML → CPU and reports what it picked in Diagnostics. |
| `gpuDeviceId` | Integer | 0–7 | 0 | Which adapter, for multi-GPU or for choosing the discrete card over an iGPU. |
| `gpuMemoryLimitMB` | Integer | 0 (= unlimited) … 4096 | **2048** | Maps to ORT's CUDA `gpu_mem_limit`. The 4 GB safety valve. |
| `maxConcurrentInference` | Integer | 1–4 | **1** | How many frames may run inference at once. Above 1 needs VRAM you probably don't have. |
| `enableMatteCache` | Boolean | — | true | Cache Controls. |
| `cacheSizeMB` | Integer | 0–4096 | 512 | Matte cache budget. |
| `preloadModel` | PushButton | — | — | Loads the model now instead of on first render, so the first frame isn't slow. |
| `clearCache` | PushButton | — | — | Drops cached mattes and the loaded session (returns VRAM immediately). |
| `threads` | Integer | 0 (= auto) … 12 | 0 | ORT intra-op thread count for the CPU EP. |

### Tab 5 — Temporal (Video)  *(addition to your brief; required by "minimise flickering")*

| Param | OFX type | Range | Default | Description |
|---|---|---|---|---|
| `temporalStrength` | Double | 0–1 | **0.35** | 0 = every frame independent. Higher = more stable, more risk of ghosting on fast motion. |
| `motionSensitivity` | Double | 0–1 | 0.5 | How quickly the stabiliser releases when the image moves. |
| `resetTemporal` | PushButton | — | — | Clears recurrent state. |

### Tab 6 — Diagnostics

| Param | OFX type | Default | Description |
|---|---|---|---|
| `statusText` | String (read-only) | `""` | Last-known state: model path, EP actually used, matte resolution, last inference ms, cache hit rate. Best-effort — hosts differ in when they refresh string params. |
| `logLevel` | Choice `Off`/`Errors`/`Info`/`Verbose` | `Errors` | Mirrors to the OFX log and to `%ProgramData%\<Vendor>\<Plugin>\log.txt`. |
| `failLoudly` | Boolean | false | Development aid — see §4.3. |
| `dumpMatte` | PushButton | — | Writes the current matte to a PNG next to the log for offline inspection. |

**Parameter-change invalidation rules** (implemented in `changedParam`):
`model`, `quality`, `inputColorSpace`, `processingResolution` → invalidate matte cache **and**
temporal state. Any Tab-2 control → invalidate temporal state only (raw matte cache stays valid, so
scrubbing refinement sliders is instant). `enable=false` → nothing to invalidate.

---

## 9. Bundle layout on Windows

```
C:\Program Files\Common Files\OFX\Plugins\
└── AiBgRemover.ofx.bundle\
    └── Contents\
        └── Win64\
            ├── AiBgRemover.ofx            # our DLL, renamed
            ├── onnxruntime.dll            # CPU, DML, or CUDA flavour — user's choice (V2)
            ├── DirectML.dll               # only with the DML flavour
            ├── (cudart64_*.dll, cublas*.dll, cudnn64_*.dll …)   # only with the CUDA flavour
            └── THIRD_PARTY_LICENSES.txt

%ProgramData%\<Vendor>\AiBgRemover\
├── models\
│   ├── manifest.json
│   ├── birefnet_lite_512_fp16_stdops.onnx
│   ├── birefnet_lite_512_fp16_deformconv.onnx
│   └── small_matte_256_fp16.onnx
└── log.txt
```

Models live outside the bundle so that (a) `Program Files` write permission is never needed,
(b) several hosts/installs share one copy, and (c) updating a model does not mean reinstalling the
plugin. `OFX_PLUGIN_PATH` and an extra `AiBgRemover/models` folder next to the bundle are also
searched, in that order, before `%ProgramData%`.

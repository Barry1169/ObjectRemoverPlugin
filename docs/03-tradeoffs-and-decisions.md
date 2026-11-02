# 03 — Trade-offs and Decision Records

Each record states the decision, the alternatives, and the reason. Where a decision is **provisional
pending a measurement on your hardware**, it says so — I will not present a guess as a result.

---

## D1 — Inference runtime: ONNX Runtime, loaded dynamically

**Decision.** ONNX Runtime via its **C API**, resolved at run time with `LoadLibraryW` +
`OrtGetApiBase` + `GetApi`. The plugin is **not** linked against `onnxruntime.lib`. Bring up the
execution providers in the order **CPU → DirectML → CUDA**, with automatic probing and a fallback
chain, so the same `.ofx` works with any ORT flavour.

**Alternatives considered.**

| Option | Why rejected |
|---|---|
| LibTorch | ~2 GB of DLLs, no meaningful advantage for inference, painful MSVC/CMake integration |
| DirectML directly (`DirectML.h`) | Locks us to DX12 only, no CPU fallback, we'd hand-write every conv/matmul graph. ORT already does this |
| OpenVINO | NVIDIA support is poor; Intel-oriented |
| TensorRT EP | Best raw speed on NVIDIA, but requires TensorRT + CUDA + cuDNN, builds engine caches per GPU, and is far too heavy for a 4 GB laptop card that is also driving Resolve |
| WinML EP | Genuinely promising on Win11 (no redistributable DLLs), but prebuilt availability is unverified (V8). Kept as a possible later EP, not a foundation |
| Static link against `onnxruntime.lib` | One binary per ORT flavour; a missing/mismatched DLL makes Resolve fail to load the plugin at all, with a confusing error. Unacceptable |

**Why dynamic loading is the right call, concretely** (verified in the ORT headers — see doc 01 §5.3):

- CUDA must be added through `OrtApi::SessionOptionsAppendExecutionProvider_CUDA[_V2]` — function
  pointers *inside the API table*. They are always resolvable and return an error status when that
  build has no CUDA. **That error status is our fallback trigger** — no `#ifdef`, no separate build.
- DML must be added through a *separate exported symbol* (`OrtSessionOptionsAppendExecutionProvider_DML`,
  or `OrtDmlApi::SessionOptionsAppendExecutionProvider_DML1` via `GetExecutionProviderApi("DML", …)`).
  Neither is reachable through the generic `SessionOptionsAppendExecutionProvider(provider_name, …)`,
  whose documented name list excludes both CUDA and DML. Only `GetProcAddress` finds it.
- Net effect: **the user chooses GPU vs CPU by which `onnxruntime.dll` they install, not by which
  plugin build they download.**

**Cost of this decision.** We hand-write ~300 lines of loader/RAII glue and mirror a handful of ORT
structs. That is cheap against the deployment flexibility, and it is fully unit-testable with a stub
DLL (which is how I will verify it here, without a GPU).

---

## D2 — Which ORT flavour to standardise on first

**Decision (provisional, needs V2 + the Phase 4 benchmark).** Ship against **CPU ORT 1.31.1** first
to prove the whole pipeline end-to-end with the fewest moving parts; then enable **DirectML**; then
evaluate **CUDA** only if DML is too slow for your workflow.

**Honest comparison — what I can state as fact vs. what must be measured:**

| | CPU (`win-x64`, 82.9 MB) | DirectML (`win-x64`, ORT **1.24.4**) | CUDA (`gpu_cuda13`, 309.1 MB) |
|---|---|---|---|
| **Fact** | Zero extra dependencies. Always works. | No CUDA/cuDNN install. Works on any DX12 GPU. | Fastest on NVIDIA. |
| **Fact** | Latest ORT (1.31.1). | **Frozen at 1.24.4** (Mar 2026); Microsoft labels it "sustained engineering" and points new projects at WinML. No prebuilt DML past 1.24.4 on GitHub or PyPI. | ORT ≥ 1.27 GPU builds default to **CUDA 13.0**; CUDA 12.8 builds also exist. Both require **cuDNN 9.x**. cuDNN 8 ↔ 9 are not ABI compatible. |
| **Fact** | — | DML has **no `DeformConv` kernel** in ORT. | CUDA **does** have `DeformConv` (`core/providers/cuda/nn/deform_conv.cu`). |
| **Fact** | — | — | `OrtCUDAProviderOptionsV2` exposes **`gpu_mem_limit`** and **`arena_extend_strategy`**. DML has no comparable hard cap. On 4 GB this is the difference between "runs" and "takes the display driver down". |
| **Fact** | — | — | CUDA route needs `cudart64_*`, `cublas64_*`, `cublasLt64_*`, `cudnn64_*`, `cufft64_*`, `curand64_*`, `nvrtc64_*`, `libnvJitLink-*` → realistically **1.5–3 GB** unless the user installs the toolkit system-wide. |
| **Fact** | Your driver (610.88) reports **CUDA 13.3 UMD**, so a CUDA-13.0-built ORT will load. RTX 2050 is Ampere sm_86 — fully supported. | | |
| **Must be measured (V13)** | Latency at 384/512/768 on your i5-11400H | Latency + first-run shader-compile stall + VRAM on your 2050 | Latency + VRAM with `gpu_mem_limit` at 1.5/2/2.5 GB |

**Trade-off summary.** DirectML = smallest install, newest-free ORT is 1.24.4, no memory cap, needs
the std-op model export. CUDA = biggest install, newest ORT, hard memory cap, native `DeformConv`,
likely fastest. CPU = guaranteed to work, and with a small model it may be good enough for stills.

Because the loader is dynamic (D1), **this is not a one-way door** — we can support all three in the
same binary and let the benchmark decide the *default*, not the *capability*.

---

## D3 — Segmentation model

**Decision.** **BiRefNet_lite (MIT)** as the quality default. A small (~5–26 MB) portrait/general
matting net as the fast/low-VRAM option. Full BiRefNet offered only as an opt-in "high-end GPU"
preset. ViTMatte as an optional stage-2 detail refiner in a late phase.

| Candidate | Quality | Speed on 4 GB | VRAM | Auto subject? | License | Verdict |
|---|---|---|---|---|---|---|
| **BiRefNet_lite** | Excellent (SOTA-class dichotomous seg) | Moderate | ~1.5–2.5 GB at 512² FP16 *(to be measured)* | ✅ | **MIT, code + weights** | **Primary** |
| BiRefNet (full) | Best | Slow | ~973 MB FP32 / 490 MB FP16 weights + large activations at 1024² | ✅ | MIT | Opt-in only |
| **RVM** | Very good, and *temporally recurrent* — exactly what we want | Fast | Low | ✅ | **GPL-3.0** | ❌ **Rejected on license.** Copyleft would force the whole plugin open. Also unmaintained since 2024. |
| **MODNet** | Good on portraits, weak on arbitrary objects | Very fast | ~100 MB | ✅ | Code Apache-2.0; **weights disputed** (Apache-2.0 per README vs CC BY-NC-SA per a downstream project) — V10 | Candidate for the fast slot *if* the weight license resolves to Apache-2.0. Fine for your personal use either way. |
| **PP-MattingV2 / PP-HumanMatting** | Good on humans, limited on objects | Very fast | Very low | ✅ | **Apache-2.0** | Strong candidate for the fast slot; needs a Paddle→ONNX export |
| **SAM 2** | Excellent masks | Slow | Hiera backbone + video memory bank; tight on 4 GB | ❌ **needs prompt points/boxes** | Apache-2.0 | ❌ Not a primary engine: it answers "segment *this*", not "what is *the* subject". Would need a detector in front of it (e.g. RT-DETR), doubling the model budget. Keep for a later click-to-pick mode. |
| **ViTMatte** | Best-in-class *edge* quality | Moderate | Moderate | ❌ **needs a trimap** | MIT | ⚠️ Not a primary engine. Excellent as **stage 2**: derive a trimap from the stage-1 alpha and let ViTMatte resolve hair. Optional, Phase 7. |
| U²-Net / IS-Net (DIS) | Good | Fast | Low | ✅ | Code Apache-2.0; weights to verify (V11) | Fallback candidate |
| RMBG-1.4 / 2.0 (BRIA) | Excellent | Fast | Low | ✅ | **Non-commercial** | ❌ Rejected |

**The trap this decision must handle (doc 01 §6.2).** BiRefNet's decoder uses deformable convolution.
ORT has `DeformConv` kernels for **CPU and CUDA but not DML**. Widely-shared "standard-op" exports
decompose it into `Gather`/`MatMul`, which runs everywhere — but a publicly measured stock
decomposition at 1024² cost **6.5 s and 10.5 GB peak memory on CPU**, versus 2.4 s for a better
re-export at 0.999 mask IoU. On your 16 GB machine a 10.5 GB peak is a crash, not a slowdown.

**So:** we ship/select the export variant to match the chosen EP —
`*_deformconv.onnx` for CUDA/CPU, `*_stdops.onnx` for DML — and we **verify the actual peak memory
of any candidate export before adopting it** (Phase 2 task, `tools/probe_model.py`). We do not adopt
a model file on the strength of a Hugging Face page.

---

## D4 — Inference resolution: run small, refine big

**Decision.** Default matte resolution **512** on the long edge (presets 256/384/512/768/1024), then
recover fine detail at full resolution with a **guided filter** using the original frame as guide
(arch §5). `Detail Refinement` exposes the strength.

**Alternatives.**

| Option | Cost | Verdict |
|---|---|---|
| Always 1024² | ~4× the compute and activation memory of 512². Public CPU measurement: 10.5 GB peak. On 4 GB VRAM alongside Resolve this will OOM. | ❌ as a default; keep as the `Maximum` preset for stills |
| 512² + bicubic upscale only | Soft, plastic edges; hair becomes a fuzzy blob | ❌ |
| 512² + **guided filter** from full-res guide | O(N), separable box filters, ~ms on 6 cores, **zero VRAM**. Recovers hair/fur that exists in the guide | ✅ |
| Tiled full-res inference | Seam artifacts, 4–9× cost, still 4 GB-bound | ❌ |
| Two-model cascade (small net → ViTMatte with derived trimap) | Best edges, 2× models, 2× VRAM | ⚠️ Optional late phase |

This is the decision that makes the project viable on your specific GPU. It trades a modest amount of
absolute segmentation accuracy for a large amount of *usable* accuracy at 1/4 the memory.

---

## D5 — Temporal consistency without GPL and without optical flow

**Decision.** OFX `SequentialRender = 2` + read `SequentialRenderStatus` + motion-adaptive EMA on the
matte, with a hard validity rule set (arch §6).

| Option | Verdict |
|---|---|
| RVM recurrent inference | Best answer technically, **GPL-3.0**. ❌ |
| Optical-flow warp of the previous matte | Costs more than the segmentation itself; flow models are another 100 MB+ and another failure mode. ❌ |
| SAM 2 video memory bank | Solves it properly, but inherits SAM 2's prompt requirement and VRAM cost. ❌ for v1 |
| **Motion-adaptive EMA gated on `SequentialRenderStatus`** | ~150 lines, no extra model, no extra VRAM, provably correct when the host is *not* sequential (state resets → plain per-frame result). Does not smear under fast motion because the weight collapses with measured frame difference. | ✅ |
| Fetch frame *t−1* from the source clip and re-run inference | Doubles decode + inference cost, and breaks `isIdentity`/caching assumptions. ❌ |

**Limitation stated up front:** an EMA is a stabiliser, not a tracker. It reduces flicker; it will not
make a fast-moving subject perfectly stable, and at high `temporalStrength` it can ghost on hard cuts.
`motionSensitivity` exists precisely to let you trade that off. I will not claim broadcast-grade
temporal coherence from this.

---

## D6 — Build system: own CMake, SDK by path, no package manager

**Decision.** Standalone CMake project. `-DOPENFX_SDK_DIR=D:/DavinciCoding/OpenFX/openfx`.
**No Conan, no vcpkg, no `add_subdirectory()` of the SDK.** ONNX Runtime is *not* a CMake dependency
at all (D1).

> **Amended by D10.** This decision originally also said "compile `Support/Library/*.cpp` into our own
> `OfxSupport` static target". That clause is withdrawn: the plugin uses the raw OFX C API and its own
> thin helper layer in `plugin/`, so **no SDK source file is compiled at all**. `OPENFX_SDK_DIR` is
> now used for its `include/` directory and nothing else. Everything else in D6 stands, and the
> amendment strictly reduces what the build depends on.

**Evidence this is safe** (doc 01 §4.2): `Support/Library/CMakeLists.txt` has no Conan reference and
no expat dependency; the top-level SDK CMake only requires `opengl_system`/`cimg`/`spdlog` when
`BUILD_EXAMPLE_PLUGINS=ON`; `HostSupport` links `expat::expat` publicly, so `add_subdirectory()`
would drag in a dependency we do not want.

**Benefits.** Your existing SDK checkout is never modified (you asked for that). No network or
package manager needed to build. Works with plain `cmake -G "Visual Studio 18 2026" -A x64` or
`Ninja` + `cl.exe`.

**Cost.** We re-implement ~30 lines of what `add_ofx_plugin()` does (the `.ofx` suffix, hidden
visibility, bundle install layout). Trivial, and it removes a Conan coupling.

**Language standard: C++20.** MSVC 19.51 supports it fully; the SDK sets `cxx_std_17` as a *minimum*
(`target_compile_features`), so C++20 is compatible. We gain `<span>`, designated initialisers,
`starts_with`, and better constexpr. **Fallback: C++17** if any incompatibility appears — no design
element depends on C++20-only features.

---

## D7 — Deviations from your inspector spec, and why

Your brief lists the inspector contents twice (§2 "Background Removal Options" and §4 "OFX Inspector
UI") and the two lists overlap and conflict. I resolved them towards §4, with four deviations:

| # | Your brief | Implemented as | Reason |
|---|---|---|---|
| 1 | §2: "Foreground Only / Background Only / Matte Preview" as separate options; §4: an **Output** group with "Composite / Foreground Only / Background Only / Matte Only" | A **single Choice** `outputMode` with those four values | Four independent booleans can be simultaneously true, which has no meaning. One choice covers exactly the same states and cannot be put in an invalid combination. |
| 2 | §2: "Refine Edges" (sounds like a toggle) + §4: "Detail Refinement" | One Double 0–100, `detailRefinement`, default 60 | A toggle gives you no control over how much hair to keep. 0 = off, so the toggle behaviour is still reachable. |
| 3 | §4 Performance: "Processing Resolution" **and** §4 Background Removal: "Segmentation Quality" | Both kept, with distinct meanings: `quality` = the AI's working resolution; `processingResolution` = whether to honour Resolve's proxy render scale | Otherwise they are the same control twice. |
| 4 | No temporal or diagnostics group | Added **Temporal (Video)** and **Diagnostics** tabs | §1 of your brief requires "consistent segmentation across consecutive video frames to minimise flickering" and §3 requires "handle missing models and inference errors gracefully". Both need user-visible controls. Diagnostics is how you will debug the model-missing path without a debugger attached to Resolve. |

Also: §2's "Auto Remove Background" is `enable`, and "Foreground Smoothing" is `edgeSmoothing`.
Everything else in §2 maps 1:1 to arch §8. **Flagging these for your approval rather than silently
choosing.**

---

## D8 — Where the AI runs relative to OFX's GPU render suite

**Decision.** Ignore `ofxGPURender.h` / the CUDA-Metal-OpenCL render suites entirely. GPU
acceleration happens *inside* ONNX Runtime.

**Reason.** The OFX GPU render suite hands the plugin a CUDA stream / Metal queue / OpenCL context
from the host and expects the plugin to produce pixels on it. Whether Resolve 21.1 Free exposes the
CUDA render suite to third-party plugins is **unverified** (V7), and even if it does, our heavy work
is a neural net that ONNX Runtime already schedules on the GPU. Interfacing ORT's allocator with a
host-supplied CUDA stream is a substantial project (ORT `OrtMemoryInfo` with a device pointer +
`CreateTensorWithDataAsOrtValue`) with a real chance of destabilising Resolve.

**Deferred, not dismissed.** Once the plugin is stable and if V7 shows the CUDA render suite is
available, a zero-copy path (Resolve's GPU frame → ORT CUDA tensor → back) is a genuine Phase 9
optimisation that would remove two full-frame PCIe copies per frame. It is explicitly out of scope
for v1.

---

## D9 — Failure behaviour: degrade, don't fail

**Decision.** Any missing dependency (ORT DLL, model file, GPU EP) results in **pass-through +
log + status message**, and `kOfxStatOK`. Exceptions never cross the L1 boundary. A debug-only
`failLoudly` bool flips this to `kOfxStatFailed` for development.

**Reason.** A plugin that returns failure, or throws, or segfaults inside Resolve can take the whole
application down and — per the Blackmagic forum threads — Resolve will then *auto-disable* the plugin
and rewrite `OFXPluginCacheV2.xml`. Degrading visibly is recoverable; crashing is not. Your brief
asks for exactly this ("Handle missing models and inference errors gracefully", "Correct behavior
when AI models are unavailable").

---

## D10 — Raw OFX C API, not the SDK's C++ support library

**Decision.** Implement the plugin against the **raw OFX C API**: export `OfxGetNumberOfPlugins` and
`OfxGetPlugin`, hand-write `pluginMain`'s action dispatch, and put our own thin helpers in `plugin/`
(`HostBridge`, `Props`, `ParamDefs`, `ParamValues`, `OfxImageAdapter`). **Compile no file from
`Support/Library`.** This supersedes the `OfxSupport` static target clause of D6.

**Evidence.**

1. `docs/reference/OpenFX_Resolve_From_Scratch.pdf` is a tested record of the raw route working on the
   *exact* target machine — Windows 11, Resolve 21, MSVC, static CRT. It loads, it exposes a
   parameter, and it renders. The support library has no equivalent record for Resolve Free 21.
2. `Support/Library` is roughly 10k lines of C++ we would link but not control, and it throws
   `OFX::Exception` types across the host boundary — the opposite of D9.
3. `Support/HostSupport` links `expat::expat` publicly. Building any part of the SDK's CMake tree
   therefore drags in a public XML dependency for a test host we do not need.
4. The support library's own `setSequentialRender(bool)` writes `int(v)`, so it can only ever express
   0 or 1. D5 needs the value **2** ("best rendered sequentially, still correct if not",
   `ofxImageEffect.h:887`), which requires writing the property directly.

**Cost.** We re-implement what `mDeclarePluginActions` and `OFX::ImageEffect` give for free: property
getters/setters, the action switch, clip and image handles. That is `plugin/HostBridge.cpp` and
`plugin/BgRemoverPlugin.cpp` — about 1200 lines, all of it code we would otherwise have to read anyway
to debug host interactions.

**Consequences we honour by hand** (each one is a place the support library would have protected us,
so each is called out in the code that implements it):

| Rule | Where it is honoured |
|---|---|
| `CreateInstance` / `DestroyInstance` return `kOfxStatOK`, **never** `kOfxStatReplyDefault` | `pluginMain` in `BgRemoverPlugin.cpp` |
| `setHost` only stores the pointer; no OFX call happens there | `OfxEntry.cpp` → `HostBridge::storeHost` |
| Suites are fetched on `kOfxActionLoad`, not before | `HostBridge::acquireSuites` |
| No C++ exception reaches the host | `try`/`catch(...)` around the whole of `pluginMain` |
| Render actions use `paramGetValueAtTime`, never `paramGetValue` (`ofxParam.h:1128`) | `ParamReader` only exposes the `AtTime` form |
| `rowBytes` may be negative | `OfxImageInfo::pixelAddress` uses `ptrdiff_t` |
| `paramDefine`'s third out-param is a **property set**, not a param handle (`ofxParam.h:1055`) | `Definer::define` in `ParamDefs.cpp` |

The first of these is not a style preference. The field guide records that answering `CreateInstance`
with `kOfxStatReplyDefault` makes Resolve Free report *"No parameter is exposed to user or OFX Plugin
[id] is not available"*, cycle create/destroy about 120 times, and never call `Render` at all.

**Rejected alternative.** Using the support library and accepting the expat dependency plus the
exception-boundary mismatch. It would have saved the ~1200 lines above, but it would have put an
untested-in-Resolve-21 code path between us and the one behaviour the guide proves works.

---

## D11 — `backgroundTransparency` means "how transparent the background becomes"

**Decision.** `backgroundTransparency` ∈ [0,1] is the transparency the removed background is given,
so the alpha handed to background pixels in Composite mode is `bgRetain = 1 - backgroundTransparency`.
**The default is 1.0** (background fully removed). 0.0 leaves the frame untouched.

Consequences, all pinned by tests in `tests/test_compositor.cpp`:

- `isPassThrough()` is true for Composite when `backgroundTransparency <= 0`, not when it is `>= 1`.
- **Background Only ignores the control entirely** and writes `A = 1 - matte`. It is a preview of what
  was removed; scaling it by `bgRetain` would make it an empty frame at the default setting, which
  would look like a broken control.
- Foreground Only and Matte Only ignore it too.

**Why this is recorded as a decision.** The first implementation had the polarity the other way round,
with a default of 0.0. Under `bgRetain = 1 - t` that default made Composite write `A = a + 1·(1-a) = 1`
with the source RGB — an exact pass-through. **A freshly installed plugin would have done nothing at
all**, and every unit test that constructed a `PipelineSettings` would have exercised the fast path
instead of the pipeline. It was caught by `tests/test_compositor.cpp` asserting the default rather than
assuming it, which is the reason that assertion exists.

The direction also matches the convention artists already know from chroma keyers: raising the
tolerance-style control removes *more*, and the inspector hint for `fallbackKeyTolerance` is written to
match ("raise it if too much background survives").

A non-finite value from a broken keyframe falls back to **1.0**, not 0.0 — a NaN should still do the
plugin's job rather than silently disable it. `PipelineSettings::sanitise()` replaces non-finite values
*before* clamping, because `std::min`/`std::max` fold a NaN into whichever bound they compare against
and the fallback would otherwise never run.

# 01 — Research and Findings

Everything below was gathered on **2026-11-02** from the OpenFX SDK source (fetched from
`AcademySoftwareFoundation/openfx@main`), the ONNX Runtime GitHub repository and release metadata
(`microsoft/onnxruntime`), PyPI package metadata, and public documentation. Items that could **not**
be confirmed are listed in § 7 rather than guessed at.

---

## 1. Repository state

```
/home/user/ObjectRemoverPlugin
└── README.md          (22 bytes: "# ObjectRemoverPlugin")
```

Single commit `24fa04c "Initial commit"`. Working branch `arena/da61a4a3-objectremoverplugin`.
This is a **greenfield** project — there is no existing code to preserve, and nothing was overwritten
to produce this design package (all files added under `docs/`).

Note the naming mismatch: the repo is called *ObjectRemoverPlugin*, but the brief describes
**background removal / subject isolation**, not object removal (inpainting). These are different
products. See V9.

---

## 2. Target environment (as supplied by you — not assumed)

| Item | Value you provided | Status |
|---|---|---|
| OS | Windows 11 Home Single Language, `10.0.22631` (23H2), x64 | Confirmed by your `Get-CimInstance` output |
| CPU | i5-11400H, 6C/12T | Confirmed |
| RAM | 15.73 GB | Confirmed |
| GPU | RTX 2050 (laptop), 4096 MiB, WDDM, 70 W cap | Confirmed by `nvidia-smi` |
| Driver | 610.88, **CUDA UMD 13.3** | Confirmed by `nvidia-smi` header |
| IDE / toolchain | VS Code, MSVC 19.51.36260, VS Build Tools 2026 (18.10.3), CMake 4.4.3, Win SDK 10.0.26100.0 | **Not independently verifiable here** — see V4 |
| Host | DaVinci Resolve Free 21.1 | **See V1 — this is the biggest open risk** |
| SDK | `D:\DavinciCoding\OpenFX\openfx\include` | Path noted; **contents/version not verifiable from here** — see V5 |
| Workspace | `D:\AI-Effects` | Noted; nothing there was read or modified |
| Free disk | Your paste was truncated before `Get-Volume` printed | **See V6** — matters, CUDA path needs several GB |

Things I deliberately did **not** invent: whether the CUDA Toolkit is installed, whether cuDNN is
installed, the exact Resolve build number, whether Visual Studio (not just Build Tools) is present,
and the free disk space.

Two observations that shape the design:

- **RTX 2050 laptop is Ampere (compute capability 8.6), 4 GB VRAM, and shares the GPU with the
  desktop compositor and with Resolve itself.** `nvidia-smi` already shows 101 MiB used at idle by
  browser processes. Realistic headroom for inference is ~3.2–3.5 GB. This rules out full-size
  BiRefNet at 1024² FP32 and rules out running several sessions concurrently.
- **Driver 610.88 reports CUDA 13.3 UMD**, so an ORT build compiled against CUDA 13.0 will run
  (CUDA minor-version compatibility). A CUDA 12.8 build would also run.

---

## 3. Sandbox reality check (what I can and cannot do here)

| Capability | Available? | Evidence |
|---|---|---|
| g++ 12.2 (Debian), C++17/20, GNU Make 4.3 | ✅ | `g++ --version` |
| CMake | ⚠️ not installed, but `cmake-4.4.4` wheel downloadable from PyPI | `pip3 download cmake` succeeded |
| `git`, `gh`, Python 3.11 | ✅ | version output |
| MSVC / Windows SDK / `cl.exe` | ❌ | Linux sandbox |
| NVIDIA GPU, CUDA, cuDNN, DirectML, DX12 | ❌ | Linux sandbox, no GPU |
| DaVinci Resolve | ❌ | Linux sandbox |
| `expat` **dev** headers (needed only by the SDK's *HostSupport* test-host library) | ❌ `libexpat1` runtime only | `/usr/include/expat*.h` missing; source is fetchable from GitHub if needed |
| Network | Restricted to `github.com`, `codeload.github.com`, `api.github.com`, `registry.npmjs.org`, `pypi.org`, `files.pythonhosted.org` | huggingface.co / onnxruntime.ai / nuget.org are **not** reachable from here |

**Consequence:** I can compile and unit-test every host-agnostic layer, and I can compile the OFX
plugin layer against the real SDK on Linux and drive it with a headless OFX host. I **cannot**
produce a Windows `.ofx`, cannot execute any ONNX Runtime EP, and cannot open Resolve. Any claim
about Resolve behaviour, GPU speed, or VRAM usage would be fabrication. Doc 04 §"Verification split"
makes this explicit per phase.

---

## 4. OpenFX SDK findings

Fetched `AcademySoftwareFoundation/openfx@main` — **release 1.5.1**, license **BSD-3-Clause**,
last pushed 2026-10-29.

### 4.1 Layout that matters to us

```
include/                 OFX C headers (ofxImageEffect.h, ofxParam.h, ofxPixels.h, ofxGPURender.h, …)
Support/include/         C++ support-library headers (ofxsImageEffect.h, ofxsProcessing.h, ofxsParam.h, …)
Support/Library/         C++ support-library sources  → builds static lib `OfxSupport`
HostSupport/             an OFX *host* implementation (needs expat) → usable as a headless test host
Examples/, Support/Plugins/   sample plugins (Basic, Invert, ChoiceParams, Retimer, Transition, GPUGain, MultiBundle, …)
cmake/OpenFX.cmake       `add_ofx_plugin()` helper — sets SUFFIX ".ofx", PREFIX "", cxx_std_17,
                         hidden visibility, and installs into
                         <PLUGIN_INSTALLDIR>/<TARGET>.ofx.bundle/Contents/Win64/
CMakeLists.txt           top level; project VERSION 1.5.1
```

### 4.2 Build-system facts that remove risk

- `Support/Library/CMakeLists.txt` builds a plain static lib and references **no** Conan package.
  `grep` for `expat` across `Support/` returns nothing. → **We can compile the OFX support library
  ourselves with zero package-manager dependencies.**
- The top-level `CMakeLists.txt` only calls `find_package(opengl_system|cimg|spdlog REQUIRED)` when
  `BUILD_EXAMPLE_PLUGINS=ON`. `find_package(EXPAT)` is *not* `REQUIRED`. `HostSupport` links
  `expat::expat` publicly, so `add_subdirectory(openfx)` pulls in an expat requirement we don't want.
  → **Recommended: do not `add_subdirectory()` the SDK. Point at it with `-DOPENFX_SDK_DIR=…`.**
  This also keeps your existing `D:\DavinciCoding\OpenFX\openfx` checkout untouched (you asked that
  nothing be modified).

  > **Corrected.** This bullet originally went on to recommend compiling `Support/Library/*.cpp` into
  > our own static target. That is withdrawn — see doc 03 §D10. `OPENFX_SDK_DIR` is now used for its
  > `include/` directory and nothing else; **no SDK source file is compiled**, so the Support
  > library's ~10k lines and `HostSupport`'s public expat dependency never enter the build.
- `add_ofx_plugin()` confirms the Windows bundle layout the plugin must ship as:
  `<Name>.ofx.bundle/Contents/Win64/<Name>.ofx`.
- `set_target_properties(... C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)` — symbols must
  be exported explicitly. On MSVC that means `__declspec(dllexport)` on **`OfxGetNumberOfPlugins` and
  `OfxGetPlugin`** (`ofxCore.h:642,650`). The SDK's own `OfxExport` macro already expands to exactly
  that on Windows and to `visibility("default")` elsewhere, so including `ofxCore.h` and matching its
  declarations is sufficient — no hand-rolled export macro is needed.

  > **Corrected twice here.** This bullet originally named `OfxGetPluginIDs`, which is not an OFX 1.x
  > entry point at all, and listed `OfxSetHost` as a required export. `OfxSetHost` is *optional*
  > (`ofxCore.h:661`, added 2020) and a host that finds it treats a `kOfxStatFailed` return as "skip
  > this plugin silently"; the configuration proven to work in Resolve Free 21.1 does not export it.
  > Both points are verified by building and inspecting the binary — see doc 05 §2.
  > The SDK branch named `fix/ofxexport-visibility` was real, and the underlying hazard (a decorated
  > name that Resolve cannot resolve) is real; it is now checked with `dumpbin /exports` as a build
  > step rather than assumed away.

### 4.3 OFX API facts that directly shape the design

| Fact | Where | Why it matters |
|---|---|---|
| `kOfxImageEffectInstancePropSequentialRender`, valid values **0 / 1 / 2**; `2` = "best to render sequentially, but will still produce correct results if not" | `include/ofxImageEffect.h:882–892` | We set **2**. Setting 1 would force Resolve to serialise our node and destroy playback performance. |
| `kOfxImageEffectPropSequentialRenderStatus` is delivered **in the render action's inArgs** | `include/ofxImageEffect.h:905`, and listed in the `kOfxImageEffectActionRender` inArgs | This is the *only* safe way to know whether our temporal state is valid for this frame. |
| Render inArgs also carry `kOfxImageEffectPropRenderQualityDraft`, `kOfxImageEffectPropThumbnailRender`, `kOfxImageEffectPropInteractiveRenderStatus`, `kOfxImageEffectPropNoSpatialAwareness`, `kOfxImageEffectPropRenderWindow`, `kOfxImageEffectPropRenderScale` | `include/ofxImageEffect.h:455–500` | Thumbnail renders must **not** load the AI model. Draft renders should drop to the cheapest quality preset. |
| Bit depths: `kOfxBitDepthByte/Short/Half/Float`; components `RGBA/RGB/Alpha` | `include/ofxImageEffect.h:1077–1102` | Half and Float are what Resolve typically hands a Color-page node. All four must be handled; "RGBA only" is not sufficient in practice. |
| `kOfxImageEffectPropPreMultiplication` on the output clip; `kOfxImageClipPropIsMask` | `include/ofxImageEffect.h:1197`, `:1275` | The single biggest source of "black halo" and "double-darkened edge" bugs. See doc 02 §"Premultiplication". |
| OFX 1.5.1 adds `kOfxImageEffectPropCPURenderSupported`, `kOfxImageEffectPropThumbnailRender`, `kOfxImageEffectPropNoSpatialAwareness` | `release-notes.md` | 1.5.1 is current; but **Resolve's implemented OFX version is unknown to me** — see V7. We should declare plugin API version conservatively (1.4) unless Resolve reports higher. |
| GPU render suites exist (`ofxGPURender.h`, CUDA/Metal/OpenCL) | `include/ofxGPURender.h` | **Not used.** Our GPU acceleration is inside ONNX Runtime, not the OFX GPU suite. This avoids depending on Resolve exposing the CUDA render suite to third-party plugins (unverified). |

### 4.4 Support-library specifics that change concrete implementation choices

Read from `Support/include/ofxsImageEffect.h` and `Support/Library/ofxsImageEffect.cpp`:

**`OFX::RenderArguments` actually contains:**

```cpp
double time; OfxPointD renderScale; OfxRectI renderWindow; FieldEnum fieldToRender;
bool isEnabledOpenCLRender, isEnabledCudaRender, isEnabledMetalRender;
void *pOpenCLCmdQ, *pCudaStream, *pMetalCmdQ;
bool sequentialRenderStatus, interactiveRenderStatus, renderQualityDraft;
```

Two consequences:
- `sequentialRenderStatus`, `interactiveRenderStatus` and `renderQualityDraft` are already parsed for
  us (`ofxsImageEffect.cpp:2120/2186/2220`) — no manual property reads needed.
- **`kOfxImageEffectPropThumbnailRender` and `kOfxImageEffectPropNoSpatialAwareness` are *not*
  plumbed into `RenderArguments`** by the 1.5.1 support library, even though the spec defines them.
  If V7 shows Resolve sets them, we read them from the raw in-args property set ourselves; otherwise
  the thumbnail fast-path is unreachable and we simply don't rely on it.

**`OFX::ImageEffectHostDescription` gives us a runtime capability probe for free** — this is how we
close V7 without guessing:

```cpp
int APIVersionMajor, APIVersionMinor;         // → which OFX features Resolve really implements
std::string hostName, hostLabel, versionLabel;
bool  supportsTiles, supportsMultiResolution, temporalClipAccess;
bool  supportsMultipleClipDepths, supportsMultipleClipPARs;
int   sequentialRender;                       // 0 = never, 1 = always, 2 = sometimes  ← our temporal gate
bool  supportsRenderQualityDraft;
bool  supportsCudaRender, supportsCudaStream; // ← Phase 9 feasibility, answered at runtime
bool  supportsStrChoice, supportsChoiceAnimation;
int   maxParameters, maxPages, pageRowCount, pageColumnCount;   // ← inspector layout limits
bool  supportsProgressSuite, supportsTimeLineSuite, supportsMessageSuiteV2;
```

Three of these directly shape the design:

1. **`sequentialRender`** tells us whether Resolve will ever give us `sequentialRenderStatus == 1`.
   If it reports `0`, temporal stabilisation is silently disabled rather than silently wrong.
2. **`supportsStrChoice`** decides how the **Model Selection** parameter is built. A classic index
   `Choice` param has its option list fixed at `describe()` time and animates by index — wrong for a
   list derived from files on disk. `kOfxParamTypeStrChoice` carries string values and is the correct
   type for a dynamically-discovered model list. We use it if the host supports it and fall back to a
   fixed Choice otherwise.
3. **`maxPages` / `pageRowCount` / `pageColumnCount` / `maxParameters`** bound the six-tab inspector
   in arch §8. If Resolve reports a small page grid, we collapse tabs into nested Groups instead of
   shipping an inspector the host cannot lay out.

**`ImageEffect::setSequentialRender(bool)`** writes `_effectProps.propSetInt(
kOfxImageEffectInstancePropSequentialRender, int(v))` — i.e. it can only ever write **0 or 1**.
To request the spec's value **2** ("best to render sequentially, but still correct if not") we must
write the property directly from our subclass, which has access to the protected `_effectProps`.
This matters: calling `setSequentialRender(true)` would request **1**, which invites the host to
serialise our node and would hurt playback.

---

## 5. ONNX Runtime findings

### 5.1 Current releases (from GitHub release metadata, 2026-11-02)

`microsoft/onnxruntime` latest: **v1.31.1** (2026-10-29). Also live: v1.31.0, v1.30.1, v1.30.0, v1.28.3.

Windows x64 assets published for v1.31.1:

| Asset | Size |
|---|---|
| `onnxruntime-win-x64-1.31.1.zip` (CPU) | 82.9 MB |
| `onnxruntime-win-x64-gpu_cuda12-1.31.1.zip` | 392.9 MB |
| `onnxruntime-win-x64-gpu_cuda13-1.31.1.zip` | 309.1 MB |

**There is no DirectML asset for 1.31.1, 1.30.1 or 1.28.3.** The same is true on PyPI:
`onnxruntime` = 1.31.1, `onnxruntime-gpu` = 1.31.1, but **`onnxruntime-directml` stops at 1.24.4**
(March 2026). Public docs describe DirectML as being in *"sustained engineering"* and advise
*"For new Windows projects, consider WinML instead."* The `microsoft/DirectML` repo README carries
a *"DirectML is in maintenance mode"* banner.

The DML execution provider **is still in the 1.31.1 source tree**
(`onnxruntime/core/providers/dml/...` present in the v1.31.1 git tree), so a from-source build with
`--use_dml` is possible — but no prebuilt binaries are published past 1.24.4.

ORT 1.31.1 also ships a **WinML execution provider** header
(`include/onnxruntime/core/providers/winml/winml_provider_factory.h`) and a
`core/providers/dml/DmlExecutionProvider/inc/IWinmlExecutionProvider.h`. This is the direction
Microsoft is pointing Windows GPU inference. Prebuilt availability is unverified — see V8.

ORT's CUDA EP is additionally being published as an **out-of-tree plugin EP**
(`plugin-ep-cuda/v0.3.0`, 2026-10-09, matched to ORT core 1.31.0, minimum core 1.24.4;
`cuda_ep_cuda13_0.3.0_win-x64.zip` = 152.5 MB). The in-tree CUDA EP is still present in the
`gpu_cuda12`/`gpu_cuda13` packages, so both routes exist.

### 5.2 CUDA EP dependency cost

From the CUDA Execution Provider requirements: ORT ≥ 1.27 GPU packages are built with **CUDA 13.0**
by default; separate **CUDA 12.8** packages exist. Both require **cuDNN 9.x**. cuDNN 8 ↔ 9 are **not**
ABI compatible. On Windows the CUDA `bin` and cuDNN `bin` directories must be resolvable — inside
Resolve's process we cannot edit `PATH`, so we would have to `AddDllDirectory` / preload the CUDA
DLLs ourselves from the plugin's own folder.

Practical footprint of the CUDA route: ORT GPU package ~310–390 MB **plus** `cudart64_*`,
`cublas64_*`, `cublasLt64_*`, `cudnn64_*`, `cufft64_*`, `curand64_*`, `nvrtc64_*`,
`libnvJitLink-*` — realistically **1.5–3 GB** of redistributable DLLs, unless the user installs the
CUDA Toolkit + cuDNN system-wide.

Upside: `OrtCUDAProviderOptionsV2` exposes **`gpu_mem_limit`** and **`arena_extend_strategy`**, which
is exactly the knob you need on a 4 GB card that is also driving a display and Resolve. DirectML
offers no comparable hard memory cap.

### 5.3 The API facts that force our loader design

Verified against `onnxruntime_c_api.h` @ v1.31.1 (`ORT_API_VERSION 31`) and the DML factory header:

1. `OrtApi::SessionOptionsAppendExecutionProvider(options, provider_name, keys, values, num_keys)`
   exists (since 1.12) — but its documented provider-name list is
   `QNN, OpenVINO, XNNPACK, WEBNN, WebGPU, AZURE, JS, VitisAI, CoreML`.
   **CUDA and DML are not in that list.** The header explicitly says: *"If an execution provider has
   a dedicated `SessionOptionsAppendExecutionProvider_<provider name>` function that should be used
   to add it."*
2. CUDA therefore goes through the **versioned OrtApi struct**:
   `SessionOptionsAppendExecutionProvider_CUDA` (`ort_c_api.h:3244`) and
   `SessionOptionsAppendExecutionProvider_CUDA_V2` (`:4071`, takes `OrtCUDAProviderOptionsV2`).
   These are *function pointers in the API table*, so they are always resolvable — and they return an
   error status if that ORT build has no CUDA compiled in. **That error status is our fallback
   trigger.**
3. DML goes through a **separate exported C symbol**, not the API table:
   `ORT_API_STATUS(OrtSessionOptionsAppendExecutionProvider_DML, OrtSessionOptions*, int device_id)`
   in `include/onnxruntime/core/providers/dml/dml_provider_factory.h` (confirmed present at
   v1.24.4). It is marked superseded by `OrtDmlApi::SessionOptionsAppendExecutionProvider_DML1`,
   obtained via `OrtApi::GetExecutionProviderApi("DML", ORT_API_VERSION, &api)` — and `"DML"` is the
   **only** provider name that function documents as supported (`ort_c_api.h:3957–3961`).

**Design consequence (this is the single most important runtime decision):**
Resolve must never fail to load our `.ofx` because an ONNX Runtime flavour is missing or mismatched.
So we **do not link against `onnxruntime.lib`**. We `LoadLibraryW(L"onnxruntime.dll")`, resolve
`OrtGetApiBase`, call `GetApi(ORT_API_VERSION)` (clamping to whatever the DLL reports), then probe
for CUDA via the API table and for DML via `GetProcAddress`. Every step has a defined failure path
ending in CPU, and CPU failing ends in *pass-through*, not a crash.

Corollary: **one `.ofx` binary works with any of the three ORT packages.** The user picks GPU or CPU
by which `onnxruntime.dll` they place next to the plugin — no rebuild.

---

## 6. AI model findings

### 6.1 License audit (from `api.github.com` repo metadata, 2026-11-02)

| Model | Repo license | Stars | Last push | Verdict for a distributable plugin |
|---|---|---|---|---|
| **BiRefNet** | **MIT** | 4,353 | 2026-10-25 | ✅ **Best.** Multiple independent sources state MIT covers code *and* pretrained weights. Actively maintained. |
| **RVM** (Robust Video Matting) | **GPL-3.0** | 9,583 | 2024-04-02 | ❌ **Rejected.** Copyleft would infect the whole plugin. Also unmaintained for 2.5 years. Painful, because its recurrent design is exactly what we want for temporal stability. |
| **MODNet** | Apache-2.0 | 4,384 | 2024-05-06 | ⚠️ Code Apache-2.0. Its README reportedly states "code, models, and demos … Apache 2.0", but at least one downstream project states the *published pretrained weights* are **CC BY-NC-SA 4.0 (non-commercial)**. **Contradiction — see V10.** Usable for personal use; not safe to redistribute until resolved. |
| **ViTMatte** | MIT | 563 | 2025-08-13 | ✅ License fine, but it **requires a trimap** — it is not an automatic subject detector. Good as an optional stage-2 detail refiner driven by a trimap derived from stage 1. |
| **SAM 2** | Apache-2.0 | 20,095 | 2026-05-30 | ✅ License fine, but needs **prompt points/boxes** — it does not answer "what is the subject" on its own. Heavy, and its video predictor carries cross-frame memory state. See doc 03. |
| **PaddleSeg** (PP-Matting / PP-HumanMatting) | Apache-2.0 | 9,413 | 2026-02-05 | ✅ Candidate for a tiny, DML-friendly, no-DeformConv fast model. Needs a Paddle→ONNX export step. |
| **U²-Net / IS-Net (DIS)** | Apache-2.0 (code) | 9,891 | 2024-06-26 | ⚠️ Weight license needs checking before redistribution — see V11. |
| **RMBG-1.4 / RMBG-2.0 (BRIA)** | non-commercial BRIA license | — | — | ❌ Rejected. Commercial use requires a paid licence. |

### 6.2 The DeformConv problem — a concrete, easy-to-miss trap

BiRefNet's decoder uses **deformable convolution**. Findings from the ORT v1.31.1 source tree:

- ORT ships `DeformConv` kernels for **CPU** (`core/providers/cpu/nn/deform_conv.cc`) and
  **CUDA** (`core/providers/cuda/nn/deform_conv.cc` + `deform_conv_impl.cu`) as a contrib op.
- There is **no DML `DeformConv` kernel**.

So an ONNX export that keeps native `DeformConv` runs on CUDA and CPU but **not** on DirectML.
Conversely, the widely-shared "standard-op" exports decompose `DeformConv` into `Gather`/`MatMul`,
which runs everywhere — but public measurements report the stock decomposition of
`BiRefNet_lite-ONNX` at 1024² costing **6.5 s and 10.5 GB peak memory on CPU**, versus **2.4 s** for
a re-export with the same 0.999 mask IoU. On a 16 GB machine, 10.5 GB peak is a crash risk.

**This is why the export variant must be chosen deliberately, per execution provider, and why the
default inference resolution is not 1024.** Both are decision records in doc 03.

### 6.3 Model portfolio proposed

| Slot | Model | Input | Weight size (approx.) | Notes |
|---|---|---|---|---|
| **Quality default** | BiRefNet_lite, MIT | 512² (384/768/1024 selectable) | ~115 MB FP16 / ~224 MB FP32 at 1024 | General subjects, animals, objects, hair. Two export variants (std-op for DML, native-DeformConv for CUDA/CPU). |
| **Fast / low-VRAM** | Small portrait-or-general matting net — PP-MattingV2 class (Apache-2.0) or MODNet (license pending V10) | 256–512² | ~5–26 MB | Real-time-ish, DML-friendly, no DeformConv. Weaker on arbitrary objects. |
| **Maximum quality** | BiRefNet (full), MIT | 1024² | ~490 MB FP16 / ~973 MB FP32 | Opt-in only; **not** expected to fit comfortably in 4 GB alongside Resolve. Must be benchmarked before being offered as anything but "High-end GPU". |
| **Optional stage 2** | ViTMatte-S, MIT | trimap + image | ~100 MB | Detail Refinement = high. Trimap derived from stage-1 alpha. Deferred to a late phase. |
| **Deferred** | SAM 2 | prompt-driven | 800 MB–2 GB | Only if you later want click-to-pick-subject. Not part of v1. |

All model files are **user-supplied, offline, discovered on disk** — the plugin never downloads
anything at render time. `tools/fetch_models.py` (run once by you) does the download and SHA-256
verification; after that the plugin is fully offline, satisfying "no internet connection during
normal operation".

### 6.4 What I could not verify from here

`huggingface.co` is not reachable from this sandbox, so I could **not** confirm that any specific
model file exists at a specific URL, its exact byte size, its input/output tensor names, or whether
a given export accepts a non-1024 input. Doc 04 therefore puts "probe the model with a 5-line Python
script and record its real tensor signature" as an explicit early task rather than assuming it.

---

## 7. Verification register

Each item is something I will not assume. **V1, V2 and V3 are the ones I need answered before I write
code.** The rest I can resolve myself during the phases noted.

| ID | Item | Why it matters | How to verify | Blocks |
|---|---|---|---|---|
| **V1** | **Does DaVinci Resolve Free 21.1 load third-party OFX plugins?** | Project-blocking. Public sources **contradict each other**: Boris FX Continuum's own release notes list "DaVinci Resolve Studio **and** DaVinci Resolve 18.x, 19.x, 20.x, 21.x" as supported hosts; Neat Video documents free+Studio support; several Resolve-focused sites say Free runs third-party OFX identically to Studio. But other sites assert Free does not load third-party OFX at all. I cannot resolve this conflict from here and will not guess. | Fastest: in Resolve Free, open **Preferences → System → Video Plugins**. If that page exists and lists/enumerates third-party plugins, you are fine. Definitive: Phase 1 delivers a do-nothing `.ofx`; drop it in `C:\Program Files\Common Files\OFX\Plugins`, restart, look for it under **Effects → OpenFX**. | Everything |
| **V2** | **Which ONNX Runtime flavour do we standardise on first?** | Determines model export variant (DeformConv vs std-op), installer size (83 MB vs ~310 MB vs ~310 MB + ~2 GB CUDA/cuDNN), and expected speed. | Doc 03 §D1 lays out the three options with costs. My recommendation: **start CPU (1.31.1, 83 MB) to prove the pipeline, then DirectML (1.24.4)**, benchmark CUDA only if DML is too slow. A benchmark harness (Phase 4) measures all three on your machine before we lock it in. | Phase 3+ |
| **V3** | **Plugin identity**: unique-ID prefix, displayed name, Resolve Effects grouping. | Baked into the binary; changing it later invalidates saved projects. | Your call. Proposal in doc 02 §"Identity". | Phase 1 |
| V4 | Exact MSVC/Build Tools/CMake/SDK versions present | Affects `CMAKE_GENERATOR`, `/std:` flag, and whether `vcpkg`/Conan is available | Run the `tools/probe-env.ps1` script supplied in Phase 0 | Phase 1 |
| V5 | What your `D:\DavinciCoding\OpenFX\openfx` checkout actually contains and which OFX version it is | If it is older than 1.5.1 we must not use 1.5-only properties | `git -C D:\DavinciCoding\OpenFX\openfx log -1` and check `include/ofxImageEffect.h` for `kOfxImageEffectPropThumbnailRender` | Phase 1 |
| V6 | Free disk space (your paste was cut off before `Get-Volume` output) | CUDA route needs several GB | Re-run the `Get-Volume` command | Phase 4 |
| V7 | Which OFX API version Resolve 21.1 implements, and which host properties it advertises | Determines whether we may use `ThumbnailRender` / `SequentialRenderStatus` / `RenderQualityDraft`, or must guard them | **The instrument is built and waiting.** `HostBridge::acquireSuites()` reads the same host property list as the SDK's own `fetchHostDescription` (`Support/Library/ofxsImageEffect.cpp:1840-1924`) and writes it to `%ProgramData%\<Vendor>\AiBgRemover\log.txt` on `kOfxActionLoad`. Set **Diagnostics → Log Level = Verbose** and read the four `host:` lines. **Still needs one run inside Resolve to answer.** Note the code guards every one of these properties regardless, because `Props::getInt` takes a fallback: a host that omits a property degrades instead of failing. | Phase 1 |
| V8 | Whether a prebuilt ORT with the **WinML** EP is publicly available | Could give DX12-backed GPU inference with no redistributable DLLs on Win11 | Check NuGet for a WinML-enabled package once V2 is decided | Phase 4 |
| V9 | Repo name vs. function: *ObjectRemover* implies inpainting; the brief is background removal | Naming, bundle ID, installer | Your call | Phase 1 |
| V10 | MODNet pretrained-weight license (Apache-2.0 vs CC BY-NC-SA 4.0) | Whether we may ship the weights | Read `ZHKKKe/MODNet` README + the checkpoint's own metadata | Phase 3 |
| V11 | U²-Net / IS-Net weight license | Same | Read the model cards | Phase 3 |
| V12 | Whether your chosen BiRefNet export accepts non-1024 inputs, and its exact tensor names/dtypes | Swin relative-position tables can be tied to 1024; a silent bad interpolation is worse than an error | `tools/probe_model.py` (Phase 2) prints the real signature and runs a 512² round-trip | Phase 2 |
| V13 | Actual VRAM/latency of each EP+model+resolution combination on the RTX 2050 | Cannot be predicted; must be measured | `tools/bench` (Phase 4) — a standalone exe using the same inference layer | Phase 4 |
| V14 | How Resolve delivers premultiplication and bit depth to this node on the Color vs Edit vs Fusion page | Determines whether edges darken or halos appear | Phase 1 diagnostic logs `kOfxImageEffectPropPreMultiplication` and supported depths per page | Phase 2 |

---

## 8. Sources consulted

- `AcademySoftwareFoundation/openfx` @ `main` (OFX 1.5.1) — `include/ofxImageEffect.h`,
  `include/ofxGPURender.h`, `Support/include/ofxsImageEffect.h`, `Support/Library/CMakeLists.txt`,
  `Support/CMakeLists.txt`, `HostSupport/CMakeLists.txt`, `cmake/OpenFX.cmake`, `CMakeLists.txt`,
  `release-notes.md`, `Support/Plugins/Invert/invert.cpp`.
- `microsoft/onnxruntime` — release metadata for v1.31.1 / v1.31.0 / v1.30.1 / v1.28.3 /
  `plugin-ep-cuda/v0.3.0`; `include/onnxruntime/core/session/onnxruntime_c_api.h` @ v1.31.1;
  `include/onnxruntime/core/providers/dml/dml_provider_factory.h` @ v1.24.4; v1.31.1 git tree
  (DeformConv kernels, DML directory, WinML headers);
  `onnxruntime/core/providers/get_execution_providers.cc`.
- PyPI JSON API — `onnxruntime` 1.31.1, `onnxruntime-gpu` 1.31.1, `onnxruntime-directml` 1.24.4,
  `onnx` 1.23.2.
- GitHub repo metadata (license / activity) for BiRefNet, RobustVideoMatting, MODNet, ViTMatte,
  sam2, PaddleSeg, U-2-Net.
- ONNX Runtime public docs: CUDA Execution Provider requirements; Install page (DirectML
  "sustained engineering" note); Execution Providers overview. `microsoft/DirectML` README
  ("maintenance mode").
- Boris FX Continuum 2026.x for OFX release notes (supported-host list); DaVinci Resolve
  OpenFX installation-folder documentation and Blackmagic forum threads on
  `C:\Program Files\Common Files\OFX\Plugins` and `OFXPluginCacheV2.xml`.
- Community ONNX exports and measurements for BiRefNet_lite (std-op re-export vs stock export).

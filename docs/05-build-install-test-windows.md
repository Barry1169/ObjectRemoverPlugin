# 05 — Windows Build, Install, Model Acquisition, and Test Recipe

**Status: draft.** The exact generator name, MSVC toolset string, and any `/std:` or warning flags
will be finalised in Phase 0 once `tools/probe-env.ps1` reports your real toolchain (V4). Nothing
of the earlier build was later exercised on Windows; the current stability patch still needs a Windows/Resolve retest.

---

## Stability retest after a Resolve crash

A prior Windows build loaded and rendered in Resolve 21.1, but the user reported crashes on
playback and when changing settings. A rapid sequence of `InstanceChanged` actions in the log is
consistent with a possible diagnostic-parameter feedback loop; the cause is not yet confirmed.
This version removes diagnostic parameter writes from render/setting callbacks, declares
instance-safe rendering, and protects reusable buffers from simultaneous rendering/cache purges.
The Diagnostics Status and Log File labels are now static defaults; consult the log for details.
Existing saved projects may retain an older label value.

1. Back up the project and `%ProgramData%\AiBgRemover\log.txt`. Close Resolve completely.
2. Get the current session's changes onto the Windows machine. From an x64 Native Tools Command
   Prompt, run `tools\build-install.bat build`. It builds, runs both core and fake-host tests,
   checks exports, and stages the bundle without installing it.
3. Back up the old `AiBgRemover.ofx.bundle`. Run `tools\build-install.bat` as administrator to
   install the new bundle. The script removes the previous installed bundle and clears the plugin
   log, so make those backups first.
4. In a **new disposable Resolve project**, apply the effect, change settings, then try playback and
   scrubbing. If Resolve crashes, stop testing and share the new plugin log and Resolve crash report.
   This patch has not yet been validated with MSVC or Resolve; success in the fake host is not proof
   that the crash is fixed. The plugin still uses a basic color key, not AI segmentation.

## 1. Prerequisites

| Component | Expected on your machine | Needed for |
|---|---|---|
| Visual Studio Build Tools 2026 (18.10.3), MSVC 19.51.36260 | ✅ per your report | compiling |
| Windows SDK 10.0.26100.0 | ✅ | compiling |
| CMake 4.4.3 | ✅ | configuring |
| OpenFX SDK at `D:\DavinciCoding\OpenFX\openfx` | ✅ | `include/`, `Support/include/`, `Support/Library/*.cpp` |
| **ONNX Runtime** (see §4) | ❓ to install | run time only — *not* a build dependency |
| **CUDA Toolkit 13.x + cuDNN 9.x** | ❓ unknown | only if you choose the CUDA EP |
| **Model files** (see §5) | ❓ to download | run time |

No Conan. No vcpkg. No Python required to build (Python is only for `tools/*.py`).

The plugin does **not** link against `onnxruntime.lib`. It resolves the ORT C API at run time, so the
build needs no ONNX Runtime headers or libraries at all. This is deliberate — see doc 03 §D1.

---

## 2. Configure and build

From a **x64 Native Tools Command Prompt for VS 2026** (or any shell where `cl.exe` is on `PATH`):

```bat
cd /d D:\AI-Effects\ObjectRemoverPlugin

cmake -S . -B build -G "Visual Studio 18 2026" -A x64 ^
      -DOPENFX_SDK_DIR="D:/DavinciCoding/OpenFX/openfx" ^
      -DCMAKE_BUILD_TYPE=RelWithDebInfo

cmake --build build --config RelWithDebInfo --parallel 12
```

Ninja equivalent (faster iteration):

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DOPENFX_SDK_DIR="D:/DavinciCoding/OpenFX/openfx"
cmake --build build --parallel 12
```

CMake options:

| Option | Default | Meaning |
|---|---|---|
| `OPENFX_SDK_DIR` | *(required)* | Root of your OpenFX checkout — the folder containing `include/` and `Support/` |
| `OFX_PLUGIN_ID_PREFIX` | `net.local` | Vendor prefix for the OFX unique ID (V3). **Set this once, before the first Resolve test — changing it later orphans saved projects.** |
| `OFX_PLUGIN_NAME` | `AiBgRemover` | Bundle/binary name |
| `BUILD_TESTS` | `ON` | Build the unit-test executable |
| `BUILD_HOST_HARNESS` | `OFF` | Build the headless OFX host (needs expat) |
| `BUILD_BENCH` | `OFF` | Build `bench.exe` (needs ONNX Runtime at *run* time only) |
| `OFX_CXX_STANDARD` | `20` | `20` or `17`; falls back cleanly (doc 03 §D6) |
| `OFX_SANITIZERS` | `OFF` | Enable ASan/UBSan for the test build |

Build outputs land in `build/`. The install step assembles the bundle:

```bat
cmake --install build --config RelWithDebInfo --prefix build\bundle
```

producing

```
build\bundle\AiBgRemover.ofx.bundle\Contents\Win64\AiBgRemover.ofx
```

### Verify the binary before installing

Do this every time, from an **x64 Native Tools Command Prompt**. A wrong export table is the single
most common reason an OFX plugin silently does not appear, and it is invisible from inside Resolve.

```bat
set OFX=build\bundle\AiBgRemover.ofx.bundle\Contents\Win64\AiBgRemover.ofx

dumpbin /exports %OFX% | findstr /i "Ofx"
dumpbin /dependents %OFX%
```

**`/exports` must show exactly these two names, spelled plainly:**

```
OfxGetNumberOfPlugins
OfxGetPlugin
```

Three things to check, all of which have bitten real plugins:

- **No leading underscore and no `@n` suffix.** `_OfxGetPlugin@4` means the definition did not match
  the `extern "C"` declaration in `ofxCore.h`, so the host cannot find it by name. Resolve will load
  the binary and then report that it contains no plugins.
- **`OfxGetNumberOfPlugins`, not `OfxGetPluginIDs`.** The former is the current OFX 1.x entry point
  (`ofxCore.h:650`); the latter is from a much older API revision and Resolve does not look for it.
- **`OfxSetHost` is deliberately absent.** It is an optional export added in 2020 (`ofxCore.h:661`).
  A host that finds it calls it first and treats a `kOfxStatFailed` return as *"skip this plugin
  silently"*. The configuration known to work in Resolve Free 21.1 exports only the two functions
  above and receives the host pointer through `OfxPlugin::setHost` instead. See doc 03 §D10. If you
  ever add it, return `kOfxStatOK` unconditionally.

**`/dependents` should list `KERNEL32.dll` and nothing else**, because the build links the MSVC
runtime statically (`OFX_STATIC_CRT=ON`). If you see `VCRUNTIME140.dll`, `MSVCP140.dll` or
`api-ms-win-crt-*.dll`, the static-CRT property did not apply — the plugin will then fail to load on
any machine without the matching redistributable, which presents as "works on my machine, missing in
Resolve". Fix it before chasing anything else:

```bat
cmake -S . -B build -DOFX_STATIC_CRT=ON
```

Both checks are also worth re-running after any change to `plugin/OfxEntry.cpp`, since that is the
only translation unit defining an exported symbol.

`tools/build-install.bat` performs the export check for you and **fails the build** if either name
is missing or decorated, rather than leaving it to be eyeballed.

### Which version of the OpenFX SDK you have

`main` is not a release. The newest tagged release is `OFX_Release_1_4_TAG`, and the repository also
carries an `OFX_Release_2_0_BRANCH`, so a checkout can be anything in between. The configure banner
reports what it found:

```
-- OpenFX SDK features: ThumbnailRender=1 SequentialRender=1 StrChoice=1 StatUnlicensed=1
-- OpenFX SDK OfxExport guard: _WIN32 (current)
```

Nothing here needs a specific vintage. Four symbols are optional and guarded with `#ifdef`, so an
older checkout compiles and simply loses the corresponding convenience:

| Symbol | OFX version | What is lost without it |
|---|---|---|
| `kOfxParamTypeStrChoice`, `kOfxParamHostPropSupportsStrChoice[Animation]` | 1.5 | Model Selection falls back to an index Choice, so adding or removing a model file can re-point a saved project |
| `kOfxImageEffectPropThumbnailRender` | 1.5 | A render is never recognised as a thumbnail render, so thumbnails cost a full inference instead of taking the fast path |
| `kOfxStatUnlicensed` | after 1.4 | One entry missing from a diagnostic name table |

The build is verified against both current `main` and `OFX_Release_1_4_TAG`; 116 checks pass and the
export table is identical in both cases.

**One thing that is not version-tolerant by accident.** `ofxCore.h` only marks the entry points
`__declspec(dllexport)` when a platform macro it recognises is set, and which macro that is changed
on 2025-11-14:

```c
/* SDKs up to 2025-11-14 */   #if defined(WIN32) || defined(WIN64)
/* SDKs after that */          #if defined(_WIN32)
```

MSVC defines `_WIN32` itself but **not** `WIN32` — that one is injected by the build system. CMake
happens to pass `/DWIN32` for MSVC, so a CMake build against an older SDK usually gets away with it.
Any other build does not, and the result is a `.ofx` with an empty export table: Resolve loads the
binary and reports that it contains no plugins, with no error anywhere. On Linux the same omission
drops the symbols out of the dynamic table under `-fvisibility=hidden`.

So `plugin/OfxEntry.cpp` does not use `OfxExport` at all. It declares the two entry points through
its own macro, built from compiler-defined macros only:

```cpp
#if defined(_WIN32) || defined(_WIN64)
#define AIBGR_OFX_ENTRY extern "C" __declspec(dllexport)
#else
#define AIBGR_OFX_ENTRY extern "C" __attribute__((visibility("default")))
#endif
```

This was found by building against the 1.4 headers: the exports vanished from the resulting binary
until the macro was introduced. Keep it.

---

## 3. Install into DaVinci Resolve

1. **Quit Resolve completely** (it scans the OFX folder only at launch).
2. Copy the whole `AiBgRemover.ofx.bundle` folder — not just the `.ofx` file — into:
   ```
   C:\Program Files\Common Files\OFX\Plugins\
   ```
   Administrator permission is required. Create the `Plugins` folder if it does not exist.
3. Copy your chosen ONNX Runtime DLLs next to the `.ofx` (§4).
4. Put the model files in `%ProgramData%\AiBgRemover\models\` (§5).
5. Start Resolve → **Effects → OpenFX → AI Matting → AI Background Remover**.
6. Confirm it is listed and enabled in **Preferences → System → Video Plugins**.

**If it does not appear:**

```powershell
# Force Resolve to rebuild its plugin list
$sup = "$env:APPDATA\Blackmagic Design\DaVinci Resolve\Support"
Rename-Item "$sup\OFXPluginCacheV2.xml" "OFXPluginCacheV2.xml.old" -ErrorAction SilentlyContinue
```

Then check, in order: (a) is it the `.ofx.bundle` *folder* in the right place, (b) does `dumpbin`
show the two undecorated exports and only `KERNEL32.dll` as a dependent (§2), (c) is the plugin
enabled in Video Plugins, (d) does
`%ProgramData%\AiBgRemover\log.txt` show a load attempt. Also note that `OFX_PLUGIN_PATH`,
if set on your system, is searched *before* the default folder — check it is not shadowing anything.

**If Resolve hangs at "Loading Video Plugins":** rename the `Plugins` folder to `Plugins.orig`,
restart to confirm Resolve is healthy, then re-add plugins one at a time. Our plugin is designed to
pass through rather than fail (doc 03 §D9), but this is the recovery path if anything goes wrong.

---

## 4. Choosing and installing ONNX Runtime

Pick **one** `onnxruntime.dll` and place it next to the `.ofx`. The plugin probes and adapts; it does
not care which you chose.

### Option A — CPU (recommended to start; smallest, always works)

Download `onnxruntime-win-x64-1.31.1.zip` (82.9 MB) from the `microsoft/onnxruntime` GitHub release
v1.31.1. Copy `lib\onnxruntime.dll` (and `onnxruntime_providers_shared.dll` if present) next to the
`.ofx`. Done. Expect seconds-per-frame, not real time.

### Option B — DirectML (GPU, no CUDA install)

The last published DirectML build is **1.24.4** (March 2026). DirectML is in "sustained engineering"
— Microsoft recommends WinML for new Windows projects — but it is functional and it is by far the
lowest-friction GPU path. Get `onnxruntime.dll` + `DirectML.dll` from the
`Microsoft.ML.OnnxRuntime.DirectML` 1.24.4 NuGet package (native `runtimes\win-x64\native\`) and copy
both next to the `.ofx`.

⚠️ **DML has no `DeformConv` kernel in ORT.** With this option you must use the `*_stdops.onnx` model
variant (doc 01 §6.2, doc 03 §D3).

### Option C — CUDA (fastest on your RTX 2050; heaviest install)

Your driver reports CUDA 13.3 UMD, so `onnxruntime-win-x64-gpu_cuda13-1.31.1.zip` (309.1 MB) is the
match. It requires **cuDNN 9.x** and the CUDA 13 runtime DLLs (`cudart64_*`, `cublas64_*`,
`cublasLt64_*`, `cudnn64_*`, `cufft64_*`, `curand64_*`, `nvrtc64_*`, `libnvJitLink-*`) — roughly
**1.5–3 GB**. Either install the CUDA Toolkit + cuDNN system-wide, or drop those DLLs next to the
`.ofx`; the plugin adds its own directory to the DLL search path so a self-contained folder works.

Because 4 GB is tight with Resolve also on the GPU, set **GPU Memory Limit = 2048 MB** in the
inspector (this maps to ORT's `gpu_mem_limit` plus `arena_extend_strategy=kSameAsRequested`).

`*_deformconv.onnx` is the better model variant here (native CUDA `DeformConv` kernel).

### Deciding between them

`tools/bench` (Phase 3.10) prints latency, peak process memory and VRAM for every EP × model ×
resolution combination on **your** machine. Choose from that table, not from this document — I have
no way to measure your hardware from here.

---

## 5. Model acquisition (one-time, online; the plugin is offline afterwards)

```powershell
cd D:\AI-Effects\ObjectRemoverPlugin
python tools\fetch_models.py --out "$env:ProgramData\AiBgRemover\models" --variant auto
```

`fetch_models.py` will:
- resolve the model set for the chosen EP (`stdops` for DML, `deformconv` for CUDA/CPU),
- download with pinned SHA-256 and refuse to write a mismatched file,
- write `manifest.json` (path, sha256, input size, normalisation, op variant, license, attribution),
- print a license summary so nothing non-redistributable slips in.

`huggingface.co` is **not reachable from this sandbox**, so the exact URLs, filenames and hashes will
be filled in during Phase 3.8 with your input or from a machine that can reach them. Until then,
treat the model list in doc 01 §6.3 as the *selection*, not as verified download links.

Minimum viable set for a first Resolve test: **one BiRefNet_lite ONNX at 512², FP16**. Roughly
100–250 MB.

**License rule for this project:** only MIT or Apache-2.0 weights ship in an installer. MODNet is
excluded until V10 is resolved; RMBG-* is excluded permanently (non-commercial).

---

## 6. Running the tests

### 6.1 Unit tests — working now

No GPU, no ONNX Runtime, no Resolve, no network needed:

```bat
ctest --test-dir build -C RelWithDebInfo --output-on-failure

:: or run the binary directly; an argument filters by substring
build\aibgr_tests.exe
build\aibgr_tests.exe premultiply
```

**Actually measured in this environment** (GCC 12.2, Linux x64 — the same source builds under MSVC):

```
116 test(s) run, 0 check(s) failed
```

The same 116 checks pass under AddressSanitizer + UndefinedBehaviorSanitizer with leak detection
enabled, so the host-agnostic layers are clean on memory errors, undefined behaviour and leaks:

```bat
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DOFX_SANITIZERS=ON ^
      -DOPENFX_SDK_DIR="D:/DavinciCoding/OpenFX/openfx"
cmake --build build-asan --target aibgr_tests
ctest --test-dir build-asan --output-on-failure
```

`OFX_SANITIZERS` is GCC/Clang only. On MSVC use `/fsanitize=address` by hand, or
`_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF)` in a Debug build.

`aibgr_ofx_tests` adds a minimal fake OFX host covering descriptor thread-safety, synchronous
parameter notifications, render/purge paths, and concurrent scratch access. This does not simulate
all Resolve behavior and does not replace the Windows/Resolve retest.

What the suites cover, mapped to the acceptance criteria in §9 of the brief:

| Suite | Criterion it protects |
|---|---|
| `test_pixel_convert` | RGBA and all four bit depths; Byte/Short are integers, not floats |
| `test_premultiply` | "No unintended black backgrounds"; zero-alpha recovers as black, not as a bright halo |
| `test_colorspace` | **"No colour shifts"** — display-referred is an exact no-op, and the transform never reaches the output |
| `test_float_image`, `test_matte` | "Avoid per-frame allocations"; grow-only capacity is asserted across a 50-frame loop |
| `test_resize` | "Preserve fine detail"; bicubic stays inside [0,1] on a step edge, area downscale conserves the mean |
| `test_compositor` | Correct alpha in all four output modes; pass-through output is bit-identical |
| `test_placeholder_matte` | "Correct behaviour when models are unavailable" |
| `test_pipeline` | End-to-end: disabled = exact copy, thumbnails never run the model, abort cancels, a failing model degrades to pass-through and still succeeds, odd and non-square resolutions |
| `test_pipeline_settings` | Defaults actually do the job; NaN and infinity survive `sanitise()` |

### 6.2 Not built yet

These are in the plan (doc 04) but do not exist in the repository, so the commands below are
aspirational until the phase that produces them lands. Do not expect them to run.

```bat
:: headless OFX host harness (Phase 2 follow-up): loads the real .ofx, feeds synthetic frames
build\hostharness\ofx_harness.exe --plugin build\AiBgRemover.ofx --case passthrough_float

:: inference benchmark (Phase 3): needs onnxruntime.dll plus a model
build\bench\bench.exe --model "%ProgramData%\AiBgRemover\models\birefnet_lite_512.onnx" ^
                      --eps cpu,dml,cuda --res 384,512,768,1024 --frames 20 --report bench.md
```

The SDK's own `HostSupport` test host cannot be used for the harness: it links `expat::expat`
publicly, and no expat development headers are assumed on your machine. The harness will call
`OfxSetHost` / `OfxGetNumberOfPlugins` / `OfxGetPlugin` directly, which is also a closer match to
what Resolve does (doc 03 §D10).

---

## 7. Manual Resolve test matrix

Save results into `docs/test-results/phaseN.md` — a real record, not a claim.

| # | Setup | Expected |
|---|---|---|
| 1 | 1080p 8-bit H.264 clip, Color page, node with plugin, `enable` off | Image bit-identical to bypassed node; plugin reports identity |
| 2 | Same, `enable` on, model present | Subject isolated, background transparent (checkerboard over a solid-color node beneath) |
| 3 | Same, model file deleted | Pass-through; Diagnostics status names the missing file; **Resolve does not crash or disable the plugin** |
| 4 | Same, `onnxruntime.dll` deleted | Pass-through; status says ONNX Runtime not found |
| 5 | All four `outputMode` values | Foreground / Background / Matte / Composite each behave as documented |
| 6 | `backgroundTransparency` 0 → 1 | Background fades in smoothly, subject unchanged |
| 7 | 4K timeline, float, Color page | Works; check peak VRAM in Task Manager; no OOM |
| 8 | Proxy playback at 1/2 and 1/4 | Follows host scale; no crash; matte still sensible |
| 9 | Scrub backwards and forwards rapidly | No ghosting, no stale matte, no crash |
| 10 | 200-frame video, moving subject, `temporalStrength` 0 vs 0.35 | Flicker visibly reduced at 0.35; no smear on fast motion |
| 11 | Hard cut inside the clip | No matte bleed across the cut |
| 12 | Two nodes using the plugin on the same timeline | Both work; model loaded once; VRAM does not double |
| 13 | Still image, `quality` = Maximum (1024) | Best edges; note the render time |
| 14 | 9:16, 2.39:1, 1:1 aspect ratios | Correct matte, no stretch |
| 15 | Detailed-hair portrait | Individual strands visible in the alpha, not a hard cut-out |
| 16 | Full render of a 30 s clip to ProRes/DNxHR | No crash, no drift, alpha preserved in the output |
| 17 | Restart Resolve, reopen the project | Settings persist; plugin still found |
| 18 | Edit page vs Color page vs Fusion page | Behaves consistently; log any per-page premultiplication difference (V14) |

---

## 8. Things that will bite, pre-emptively noted

1. **Missing `__declspec(dllexport)` on the two `Ofx*` entry points** → plugin does not appear, no
   error, no crash, nothing in the log. Always `dumpbin /exports` and `dumpbin /dependents` first
   (§2). Do not export `OfxSetHost`: a host that finds it treats a `kOfxStatFailed` return as "skip
   this plugin silently" (docs/03, D10). The project guards against this class of failure with its
   own export macro instead of the SDK's `OfxExport` — see "Which version of the OpenFX SDK you
   have" in §2 for why.
2. **`LNK2038: mismatch detected for RuntimeLibrary`** → one target was compiled `/MT` and another
   `/MD`. MSVC stamps the runtime choice into every object file and refuses to link a mix.
   `MSVC_RUNTIME_LIBRARY` is a **per-target** property whose default is `/MD`, so setting it on the
   plugin and the core library still leaves every other target — the test executable, and anything
   added later — silently on `/MD`. This project sets `CMAKE_MSVC_RUNTIME_LIBRARY` **once, before any
   target is created** (it is read at target-creation time and is not retroactive), and asserts at
   configure time that every target resolved to the same value. Do not reintroduce a per-target
   `set_property`. If the error appears after pulling this fix, the build directory still holds
   objects compiled under the old flags — delete it and re-run.
3. **`.ofx` is a renamed DLL** — the CMake target must set `SUFFIX ".ofx"` and `PREFIX ""`, or Resolve
   will not find it.
4. **Bundle layout** must be `<Name>.ofx.bundle/Contents/Win64/<Name>.ofx`. A loose `.ofx` is ignored.
5. **`onnxruntime.dll` not next to the `.ofx`** → Windows will not search `Program Files\Common Files`
   by default; the plugin adds its own directory to the DLL search path, but keep the DLL adjacent.
6. **CUDA DLL search inside Resolve** — Resolve's process `PATH` will not include your CUDA folders.
   The plugin must `AddDllDirectory`/preload from its own folder. This is a known trap and is handled
   in `util/win/Paths`.
7. **4 GB VRAM shared with the desktop and Resolve** — cap ORT's arena. Do not set
   `maxConcurrentInference` above 1.
8. **Antivirus / SmartScreen** on an unsigned DLL in `Program Files` can silently block the load.
   Code-signing is out of scope here but worth knowing.
9. **Resolve auto-disables a plugin that crashed at startup** and records it in
   `OFXPluginCacheV2.xml`. After any crash, check that file.
10. **`Half` bit depth** — do not assume `kOfxBitDepthHalf` never arrives. Resolve does use it.
11. **Premultiplication differs by page/context** (V14). The `premultiplyOutput` escape hatch exists
    for exactly this; use it to diagnose rather than rebuild.

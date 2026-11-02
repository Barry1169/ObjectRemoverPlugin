# 05 — Windows Build, Install, Model Acquisition, and Test Recipe

**Status: draft.** The exact generator name, MSVC toolset string, and any `/std:` or warning flags
will be finalised in Phase 0 once `tools/probe-env.ps1` reports your real toolchain (V4). Nothing
here has been executed on Windows.

---

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

**Verify the exports before installing** (a missing export is the #1 reason an OFX plugin silently
does not appear):

```bat
dumpbin /exports build\bundle\AiBgRemover.ofx.bundle\Contents\Win64\AiBgRemover.ofx | findstr /i "Ofx"
```

Expect exactly:

```
OfxGetPluginIDs
OfxGetPlugin
OfxSetHost
```

---

## 3. Install into DaVinci Resolve

1. **Quit Resolve completely** (it scans the OFX folder only at launch).
2. Copy the whole `AiBgRemover.ofx.bundle` folder — not just the `.ofx` file — into:
   ```
   C:\Program Files\Common Files\OFX\Plugins\
   ```
   Administrator permission is required. Create the `Plugins` folder if it does not exist.
3. Copy your chosen ONNX Runtime DLLs next to the `.ofx` (§4).
4. Put the model files in `%ProgramData%\<Vendor>\AiBgRemover\models\` (§5).
5. Start Resolve → **Effects → OpenFX → AI Matting → AI Background Remover**.
6. Confirm it is listed and enabled in **Preferences → System → Video Plugins**.

**If it does not appear:**

```powershell
# Force Resolve to rebuild its plugin list
$sup = "$env:APPDATA\Blackmagic Design\DaVinci Resolve\Support"
Rename-Item "$sup\OFXPluginCacheV2.xml" "OFXPluginCacheV2.xml.old" -ErrorAction SilentlyContinue
```

Then check, in order: (a) is it the `.ofx.bundle` *folder* in the right place, (b) does `dumpbin`
show all three exports, (c) is the plugin enabled in Video Plugins, (d) does
`%ProgramData%\<Vendor>\AiBgRemover\log.txt` show a load attempt. Also note that `OFX_PLUGIN_PATH`,
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
python tools\fetch_models.py --out "$env:ProgramData\<Vendor>\AiBgRemover\models" --variant auto
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

```bat
:: unit tests (no GPU, no ONNX Runtime, no Resolve needed)
ctest --test-dir build -C RelWithDebInfo --output-on-failure

:: sanitiser build
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOFX_SANITIZERS=ON ^
      -DOPENFX_SDK_DIR="D:/DavinciCoding/OpenFX/openfx"
cmake --build build-asan && ctest --test-dir build-asan --output-on-failure

:: headless OFX host harness: loads the real plugin, feeds synthetic frames, checks alpha
build\hostharness\ofx_harness.exe --plugin build\AiBgRemover.ofx --case passthrough_float
build\hostharness\ofx_harness.exe --plugin build\AiBgRemover.ofx --case passthrough_byte

:: inference benchmark (needs onnxruntime.dll + a model)
build\bench\bench.exe --model "%ProgramData%\<Vendor>\AiBgRemover\models\birefnet_lite_512.onnx" ^
                      --eps cpu,dml,cuda --res 384,512,768,1024 --frames 20 --report bench.md

:: leak check on Windows
"C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\appverif.exe"   :: enable Heaps + Locks for Resolve.exe? NO -
:: instead run the harness under Application Verifier, and use _CrtSetDbgFlag in Debug builds
```

I can run the first two of those here on Linux and report actual output. The `bench.exe` and
harness-on-Windows runs are yours.

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

1. **Missing `__declspec(dllexport)` on the three `Ofx*` entry points** → plugin does not appear, no
   error. Always `dumpbin /exports` first.
2. **`.ofx` is a renamed DLL** — the CMake target must set `SUFFIX ".ofx"` and `PREFIX ""`, or Resolve
   will not find it.
3. **Bundle layout** must be `<Name>.ofx.bundle/Contents/Win64/<Name>.ofx`. A loose `.ofx` is ignored.
4. **`onnxruntime.dll` not next to the `.ofx`** → Windows will not search `Program Files\Common Files`
   by default; the plugin adds its own directory to the DLL search path, but keep the DLL adjacent.
5. **CUDA DLL search inside Resolve** — Resolve's process `PATH` will not include your CUDA folders.
   The plugin must `AddDllDirectory`/preload from its own folder. This is a known trap and is handled
   in `util/win/Paths`.
6. **4 GB VRAM shared with the desktop and Resolve** — cap ORT's arena. Do not set
   `maxConcurrentInference` above 1.
7. **Antivirus / SmartScreen** on an unsigned DLL in `Program Files` can silently block the load.
   Code-signing is out of scope here but worth knowing.
8. **Resolve auto-disables a plugin that crashed at startup** and records it in
   `OFXPluginCacheV2.xml`. After any crash, check that file.
9. **`Half` bit depth** — do not assume `kOfxBitDepthHalf` never arrives. Resolve does use it.
10. **Premultiplication differs by page/context** (V14). The `premultiplyOutput` escape hatch exists
    for exactly this; use it to diagnose rather than rebuild.

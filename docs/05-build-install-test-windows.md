# 05 — Windows Build, Install, Model Acquisition, and Test Recipe

**Current implementation:** U2NetP AI on CPU is optional. The earlier build passed Windows inference
and produced renders in Resolve; the new painted-mask feature still requires Windows/Resolve testing.
The previously reported playback crashes remain unproven fixed. Back up projects, logs and the existing bundle before testing in a disposable project.

For the optional manually painted guidance PNG, see [06-guidance-mask.md](06-guidance-mask.md).

## Quick start for this checkout

Close Resolve completely. In an x64 Native Tools Command Prompt:

```bat
cd /d D:\AI-Effects\ObjectRemoverPlugin-Fix
git pull --ff-only
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup-ai.ps1
tools\build-install.bat build
```

The setup downloads the U2NetP model (~5 MB) and Microsoft CPU runtime ZIP (~78 MB), verifies their
SHA-256 hashes and installs them under this checkout's `models` and `runtime` directories. It never
overwrites an existing file. An existing model must match the expected hash; an existing DLL is left
untouched and is not verified by the script. Downloads require internet; rendering is offline.

Then, with Resolve still closed, run the following **in an Administrator command prompt**:

```bat
cd /d D:\AI-Effects\ObjectRemoverPlugin-Fix
tools\build-install.bat
```

Start Resolve and test in a **new disposable project**. In Background Removal > Model Selection,
choose **U2NetP (AI, CPU)**. Existing effect instances may keep **No AI model (built-in colour key)**;
change them explicitly. Use Output Mode `Matte Only` to inspect the mask; Matte Bias changes the
mask. Background Colour Tolerance/Softness only affect the colour key, not AI. If AI is missing,
check both files below, close and restart Resolve, and inspect the log. If an AI model fails to load,
the colour key is used; a per-frame inference error passes the original image through and is logged.
Look for `AI loaded` and a non-thumbnail render with `fallback=0`; also check for any
`matte source` errors, which cause that frame to pass through.

* `D:\AI-Effects\ObjectRemoverPlugin-Fix\models\u2netp.onnx`
* `D:\AI-Effects\ObjectRemoverPlugin-Fix\runtime\onnxruntime.dll`
* Preferred log: `D:\AI-Effects\ObjectRemoverPlugin-Fix\log.txt`

The build script sets the workspace to its checkout directory, so moving the checkout requires
rebuilding and reinstalling. If the log cannot be written there, the plugin tries
`%ProgramData%\AiBgRemover\log.txt`, then `%LOCALAPPDATA%\AiBgRemover\log.txt`. A static
inspector log label in an old project can be stale. The installer preserves prior bundle logs.

U2NetP works on each frame independently at a fixed 320×320. CPU playback may be slow; no GPU,
temporal stability, hair refinement or prompt-based object selection is implemented. The model may
produce imperfect masks. The plugin uses two CPU inference threads per instance. Windows may need
the appropriate Microsoft Visual C++ runtime for the separately downloaded ONNX Runtime DLL.

## Provenance and licensing

The ONNX Runtime C API header and license are included under `third_party/onnxruntime` (MIT).
The setup script downloads the Microsoft ONNX Runtime 1.23.2 Windows CPU release (MIT) and the
U2NetP ONNX model published by [rembg](https://github.com/danielgatis/rembg). The
[U-2-Net source repository](https://github.com/xuebinqin/U-2-Net) uses Apache-2.0, but that alone
does not establish licensing of these particular exported weights or their training data. Verify the
weights' terms independently before redistribution or commercial use. The project does not bundle
the model or DLL. Do not confuse checksum verification with a license grant.

## Older detailed build notes

The sections below are historical planning notes. Where they conflict with the quick start above,
the quick start and current implementation take precedence.

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

The plugin does **not** link against `onnxruntime.lib`. It resolves the ORT C API at run time using the vendored header; the build needs no separately installed ONNX Runtime libraries. This is deliberate — see doc 03 §D1.

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
3. Use `tools\setup-ai.ps1` to install the supported CPU runtime and model into the checkout (§4–5).
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
the selected `log.txt` show a load attempt (see the log-location note below). Also note that `OFX_PLUGIN_PATH`,
if set on your system, is searched *before* the default folder — check it is not shadowing anything.

**If Resolve hangs at "Loading Video Plugins":** rename the `Plugins` folder to `Plugins.orig`,
restart to confirm Resolve is healthy, then re-add plugins one at a time. Our plugin is designed to
pass through rather than fail (doc 03 §D9), but this is the recovery path if anything goes wrong.

---

## 4–5. Runtime and model acquisition

Use `tools\setup-ai.ps1` as described in the quick start above. Other execution providers and other
models discussed in older planning documents are **not implemented** in this build.

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

This earlier result predates the AI implementation; the AI code has since also been tested with
AddressSanitizer and UndefinedBehaviorSanitizer on Linux. Sanitizers do not establish Windows or
Resolve compatibility:

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

These are **tests to perform**, not claims about results. Use a disposable project and stop if
Resolve crashes; share the plugin log and Resolve crash report.

1. With AI files installed, select U2NetP, render a frame, and inspect Matte Only. Confirm the log
   says `AI loaded` and the render reports `fallback=0` without a matte-source error.
2. Adjust Matte Bias and output mode; compare against the colour-key-only tolerance and softness.
3. Test scrubbing, playback, two effect instances and a short export, watching for crashes or stalls.
4. Test a missing or corrupt model/runtime separately with Resolve closed before changing files;
   confirm the colour-key fallback and explanatory log. Restore the files afterwards.
5. Reopen the project and check the selection persists. Existing key instances stay on the key.

CPU speed, visual quality, hair detail, playback stability and every Windows-specific behavior are
not established by Linux tests. Temporal/GPU/cache controls are not functional yet.

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
5. **AI runtime path** — the plugin loads the DLL from the configured checkout's `runtime` folder.
6. **GPU providers** — not implemented yet; this version uses CPU only.
7. **Per-instance resources** — each effect instance creates its own CPU inference session.
8. **Antivirus / SmartScreen** on an unsigned DLL in `Program Files` can silently block the load.
   Code-signing is out of scope here but worth knowing.
9. **Resolve auto-disables a plugin that crashed at startup** and records it in
   `OFXPluginCacheV2.xml`. After any crash, check that file.
10. **`Half` bit depth** — do not assume `kOfxBitDepthHalf` never arrives. Resolve does use it.
11. **Premultiplication differs by page/context** (V14). The `premultiplyOutput` escape hatch exists
    for exactly this; use it to diagnose rather than rebuild.

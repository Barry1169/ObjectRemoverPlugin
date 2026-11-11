# AI Background Remover for DaVinci Resolve

An experimental Windows OpenFX plugin for making an AI-assisted selection inside
DaVinci Resolve. Draw a rough box around an object in the viewer and use more
boxes to add or remove areas. The selector uses the EfficientSAM-Ti ONNX models
on the CPU; it does **not** require GIMP, a guidance PNG, or a dedicated GPU.

This project removes backgrounds using a matte. It does **not** currently fill
in the background behind a removed object (inpainting), and it does not track
objects through a video.

> **Project status:** Experimental, not a polished installer. A Windows build has
> been used in Resolve, but behavior and stability have not been verified across
> all machines, hosts, footage or Resolve versions. Previously reported playback
> crashes have not been proven fixed. Back up your project and any installed
> plugin bundle, and try a disposable project before relying on it for work.

## What it can do

- **AI prompt selection:** draw up to 32 ordered add/remove prompts in the
  Resolve viewer — loose rectangles around a subject, or single clicks on it.
  EfficientSAM guesses a mask for each prompt. Prompts are hints, not precise
  clipping boundaries.
- **Detail recovery:** an optional alternate-mask mode can recover some missed
  foliage or branches, but can also include unwanted sky or objects.
- **Viewer controls:** add box, remove box, add point, remove point, undo,
  clear, stop drawing, and toggle detail recovery. The image is unchanged until
  an add prompt is drawn.
- **Other experimental modes:** manual brush corrections, guided selection,
  optional PNG guidance, a legacy automatic U2NetP matte, and a simple
  border-colour key. These are separate from the AI prompt workflow.

**Limitations:** Fine branches, hair, fences and similar details may be missed;
background can remain; a box may select the wrong subject. Results vary with the
image. There is no motion tracking or GPU inference. CPU processing can be slow,
especially on video or with several effect instances. Model input is limited to
at most 1024 pixels on the long edge before the result is upscaled. Check the
final render, project reload and proxy/full-resolution alignment yourself.

## Installation for a new user (Windows x64)

There is currently **no ready-made installer or prebuilt release** in these
instructions: users build the plugin from source. The steps below use the default
`main` branch. Do not remove an existing installation without backing it up.

### 1. Prerequisites

- Windows x64 and DaVinci Resolve with OFX support. The workflow has been used
  in Resolve on Windows; no general compatibility guarantee is implied.
- Git, CMake 3.20 or newer, and Visual Studio 2026 / Build Tools with the C++
  desktop workload and Windows SDK. The provided build script selects the
  `Visual Studio 18 2026` generator; other versions require adjusting the script
  or using the [manual CMake instructions](docs/05-build-install-test-windows.md).
- Internet access for the initial source, SDK, runtime and model downloads.
- Disk space for Visual Studio, source/build files, ONNX Runtime and models. CPU
  speed and memory affect responsiveness; no minimum RAM has been established.

Close Resolve fully before installation. The example uses `C:\AI-Effects`; choose
another writable location if needed. In an **x64 Native Tools Command Prompt**, run:

```bat
mkdir C:\AI-Effects
cd /d C:\AI-Effects
git clone https://github.com/Barry1169/ObjectRemoverPlugin.git
git clone https://github.com/AcademySoftwareFoundation/openfx.git
cd /d C:\AI-Effects\ObjectRemoverPlugin
set "OPENFX_SDK_DIR=C:\AI-Effects\openfx"
```

If `C:\AI-Effects` already exists, `mkdir` may report that fact; continue. If a
clone destination already exists, don't overwrite it: use an appropriate existing
checkout or choose a different path. `OPENFX_SDK_DIR` must point to the SDK folder
containing `include\ofxImageEffect.h`. Keep this command window open so the
setting remains available.

### 2. Download the runtime and model files

From the plugin checkout in the same Command Prompt:

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup-ai.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\setup-selection.ps1
```

The first script installs a CPU ONNX Runtime (1.23.2) **and also downloads the
legacy U2NetP model**, even though U2NetP is not needed for AI prompt selection.
The second downloads the official EfficientSAM-Ti encoder and decoder (about
41 MB total). The scripts verify downloaded hashes and do not overwrite existing
files; an existing runtime is left unchanged and is not verified. Check the
scripts' output and stop if a download or hash check fails.

The U2NetP export's terms are **not yet confirmed for commercial use or
redistribution**. If you do not want it, instead install a compatible x64 CPU
ONNX Runtime 1.16+ yourself from the
[official ONNX Runtime releases](https://github.com/microsoft/onnxruntime/releases)
in `runtime\onnxruntime.dll`, retaining any required provider DLLs and license
notices, then run only `setup-selection.ps1`. The documented setup script uses
`onnxruntime-win-x64-1.23.2.zip`, SHA-256
`0b38df9af21834e41e73d602d90db5cb06dbd1ca618948b8f1d66d607ac9f3cd`.
Consult the release's own instructions and terms for the exact files required.

After setup, the box selector expects these files under the **checkout**, not
inside the plugin bundle:

```text
ObjectRemoverPlugin\runtime\onnxruntime.dll
ObjectRemoverPlugin\models\efficientsam_ti_encoder.onnx
ObjectRemoverPlugin\models\efficientsam_ti_decoder.onnx
```

The compiled plugin remembers the checkout path. Keep that folder and the files
there; if you move the checkout, rebuild and reinstall. Model and runtime binaries
are not committed to this repository.

### 3. Build, test and install

With Resolve still closed, build first in the same command window:

```bat
tools\build-install.bat build
```

The script configures and builds the plugin, runs the automated tests, stages a
bundle and checks its exports. The ordinary test run does not by itself prove
that your runtime/models or Resolve integration work. Stop if any step fails.

To install, open an **Administrator x64 Native Tools Command Prompt**, return to
the checkout, and set the SDK path again because environment settings are per
window:

```bat
cd /d C:\AI-Effects\ObjectRemoverPlugin
set "OPENFX_SDK_DIR=C:\AI-Effects\openfx"
tools\build-install.bat
```

The script rebuilds and tests, then installs
`AiBgRemover.ofx.bundle` under `C:\Program Files\Common Files\OFX\Plugins`.
It replaces an existing bundle of the same name, so back it up first. It also
attempts to refresh Resolve's OFX plugin cache. Start Resolve only after the
script succeeds.

## Use the AI prompt selector

1. In a new test project, place a clip or image on the Edit page. Add **AI
   Background Remover** from **Effects → OpenFX → AI Matting** to the clip.
2. Select the clip/effect and enable **Open FX Overlay** in the viewer. A new
   effect starts in **Ready**, with the source unchanged.
3. Click **ADD BOX** and drag a loose rectangle around the subject, or click
   **ADD POINT** and click once on it. Releasing the first add prompt activates
   AI prompt selection. Allow time for CPU inference.
4. Draw more prompts to refine the selection: **ADD BOX** / **ADD POINT** unite
   their prediction with the current one, and **REMOVE BOX** / **REMOVE POINT**
   subtract the object found there. The order matters. Boxes and points share a
   limit of 32 prompts. **UNDO** removes the last action; **CLEAR** removes all
   saved actions; **OFF** stops drawing but doesn't turn off the effect. Prompts
   disappear on release.
5. Try **DETAIL ON** if details are missing, and turn it off if it makes the
   mask worse. Inspect **Matte Only** or **Foreground Only** in the Inspector
   and check the final output.
6. On a video, every frame is new, so the AI runs again on each one and playback
   is slow. Set **AI Prompts: Recompute** to **Reuse while playing (smooth)** in
   the Inspector to keep the current selection during playback and recompute
   when it stops; switch back to **Every frame (accurate)** before rendering.
   See [video and playback speed](docs/08-ai-box-selection.md#video-playback-and-speed).

If the toolbar is not visible, ensure the effect is selected and Open FX Overlay
is enabled, then try zooming out or fitting the image. Inspector controls are
also available. Clearing all prompts returns the effect to the unchanged image.
See [the AI prompt guide](docs/08-ai-box-selection.md) for further detail.

## Troubleshooting and updates

- **Plugin missing:** make sure the build/install succeeded, Resolve was closed,
  and the bundle is in the OFX Plugins folder. Restart Resolve. See the
  [Windows guide](docs/05-build-install-test-windows.md).
- **Image remains unchanged after a prompt:** confirm the effect is enabled and the
  runtime and both model files exist at the workspace paths above. Box mode
  passes the source through if loading or inference fails rather than silently
  applying the legacy colour key. Read `log.txt` in the checkout; after
  installing missing files use **Retry AI Load** or restart Resolve. Fallback
  log locations include `%ProgramData%\AiBgRemover\log.txt` and
  `%LOCALAPPDATA%\AiBgRemover\log.txt`.
- **Playback is slow on a video, but a still image is fine:** this is expected on
  CPU. A still has identical pixels on every frame, so the cached matte is reused;
  a video changes every frame, so the model runs once per frame. Set **AI
  Prompts: Recompute** to **Reuse while playing (smooth)** or **Once per prompt
  change (stills)**, set **CPU Threads** to `0` so every core is used, and use as
  few prompts as possible. See
  [the AI prompt guide](docs/08-ai-box-selection.md#video-playback-and-speed).
- **Output is inaccurate:** try a different loose box or point, add/remove prompts, or
  toggle detail recovery. None of these guarantees a correct matte.
- **Update an existing checkout:** back up projects and the installed bundle,
  close Resolve, pull the intended branch using `git pull --ff-only`, then
  rebuild and reinstall. Test old projects: the `AIBGR4` box format and the
  `AIBGR5` format written when point prompts are used are not readable by older
  plugin builds, and defaults in old projects may differ.
- **Remove the plugin:** close Resolve and use `tools\build-install.bat uninstall`
  from an Administrator command prompt. This removes the installed bundle; it
  does not delete your checkout, model files, or runtime.

## Licenses and commercial use

The repository currently has **no top-level project LICENSE file**. Individual
source files contain MIT SPDX headers, but that is not a substitute for a clear
project-wide license and copyright review. The repository being public does not
by itself grant everyone permission to use or redistribute the whole project.
The owner should select a project license before presenting it as open source.

Third-party components have their own terms. The official
[EfficientSAM repository](https://github.com/yformer/EfficientSAM) is Apache-2.0,
a permissive license that allows commercial use subject to its conditions.
Review the precise ONNX weights, provenance, notices and any applicable training
or data terms for your distribution. ONNX Runtime and the OpenFX SDK have their
own license obligations, and the compiled plugin includes `stb_image`. The
optional legacy U2NetP export has unresolved weights licensing; do not assume
its code license automatically clears those weights. No BRIA model is used by
the current box selector.

See [third-party notices](packaging/THIRD_PARTY_LICENSES.txt) and the upstream
licenses. Before selling or distributing a binary, review rights for **every**
component and model, include required notices, and consider professional legal
advice. This README is not legal advice or a grant of rights.

## More documentation

- [AI prompt selection (boxes and points) and limitations](docs/08-ai-box-selection.md)
- [Experimental viewer brush](docs/07-viewer-brush.md)
- [Windows build, installation and diagnostics](docs/05-build-install-test-windows.md)
- [Documentation index](docs/README.md)

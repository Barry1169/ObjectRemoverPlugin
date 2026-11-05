# Experimental AI box selection inside Resolve

This is a separate, prompt-based selector, not GIMP/GEGL and not a brush. Drag a
rough rectangle around an object; the EfficientSAM-Ti model predicts an object mask
inside/near it. Add more boxes to union their predictions; remove boxes subtract
predictions. It is not guaranteed to select the intended object, can miss thin
branches or poles, and may include nearby sky. Boxes are prompts, not clipping
rectangles. It does not track moving objects.

## Setup and use

- The existing CPU ONNX Runtime is required. Run `tools\setup-selection.ps1` to
  download the official encoder and decoder (approximately 41 MB total), with
  SHA-256 verification. No existing files are overwritten. The model files stay
  in the workspace `models` folder, not inside the installed plugin bundle.
- Close Resolve before installing a new plugin, back up the existing bundle and
  save a separate project copy. Reopen Resolve with the effect selected and
  Open FX Overlay enabled.
- A newly added effect starts in **Ready (no automatic removal)**: the image is
  unchanged and neither the legacy U2NetP model nor the colour key is run. **AI add
  box** is the default viewer tool. Click **ADD BOX** in the toolbar if necessary,
  then drag a loose rectangle around the object and release. The first box activates
  **AI box selection** and runs EfficientSAM; there is no U2NetP/key base matte in
  this mode. No external editor or file is needed. Until a valid add box exists,
  including after clearing all boxes, the image remains unchanged.
- The viewer toolbar has labelled **ADD BOX**, **REMOVE BOX**, **DETAIL OFF/ON**,
  **UNDO**, **CLEAR**, and **OFF** buttons. The yellow border marks a selected tool
  or enabled detail recovery. **OFF** stops drawing but does not disable the effect.
  The bar is positioned relative to the viewport when the host supplies compatible
  OpenGL matrices; otherwise it falls back to the source image corner. If it is not
  visible, zoom out/fit, enable Open FX Overlay, or use the Inspector controls.
- Additional add boxes union predictions; remove boxes subtract predictions in order.
  A box is a prompt, not a clipping boundary. Boxes disappear on release. **UNDO**
  removes the last action; **CLEAR** deletes all saved actions including old strokes.
  Up to 32 boxes are accepted. Old manual/guided brush tools remain in the Inspector.
- **DETAIL ON** (also **AI Box: Recover More Detail** in the Inspector) tries the
  closest alternate AI mask for each add box and unions it with the best prediction.
  On the supplied tree test image it recovers part of the missing left foliage, but
  may retain sky or other unwanted areas and is not guaranteed to recover thin
  branches. It is off by default and can be switched off without changing boxes.
- Inspect Output → Matte Only or Foreground Only. Check zoom, project save/reopen,
  proxy/full-resolution alignment and final output in Resolve; the revised toolbar
  and detail mode have not yet been tested in Windows Resolve.

The model runs on CPU. The embedding is cached per effect instance and reused while
the input pixels remain unchanged; changing frames requires re-encoding. Model output
is at most 1024 pixels on its long edge and edge-aware upscaled. It has no motion
tracking. If the model/runtime is missing or inference fails, box mode passes the
original frame through unchanged and records the error in `log.txt`; **Retry AI Load**
retries loading on the next render. The experimental detail mode can produce false
positives or fail to recover branches.

The legacy automatic U2NetP/key modes are still available explicitly under Viewer
Selection Method; the **Automatic Model (legacy)** setting applies only to those
modes. Its value does not make U2NetP run in Ready or AI box selection mode.
Existing saved explicit choice indices remain unchanged, but some hosts may omit
parameters saved at their old defaults and use new defaults when reloading a project.
Back up projects and verify existing instances after updating.

Projects containing boxes use a new `AIBGR4` storage format. Older builds cannot
read it, so keep a separate backup before downgrading. Existing project parameter
names and old choice indices remain unchanged.

## Optional BRIA-assisted add boxes (non-commercial, experimental)

This is **off by default** and does not replace the existing EfficientSAM mode.
It runs the official quantized BRIA RMBG-1.4 saliency model on the image crop
inside each **add box**, combining its soft masks in stroke order. **Remove boxes**
still use EfficientSAM against the full frame; their masks are subtracted. A later
add can restore areas. BRIA is not a promptable object selector: it may select
multiple salient objects or background, miss the desired subject, cut off anything
outside the box, or produce edges/artifacts. For the sample tree, a broad add box
around the tree followed by a broad remove box around the cloud produced a promising
result, not a guarantee for other images. Use Undo/Clear as usual. Box coordinates
stay in the project; no external PNG is needed. No tracking is provided.

**License:** BRIA's [model card](https://huggingface.co/briaai/RMBG-1.4)
says the model is source-available for non-commercial use and commercial use
requires a separate agreement with BRIA. Read the linked license and model card
and obtain permission if your intended use needs it. A student or open-source
project is not automatically exempt for every use. The plugin code's license
does not grant model rights. We do not distribute the weights; check redistribution
and modification permissions with BRIA before bundling or sharing them.

With Resolve closed and the plugin built/installed, set up ONNX Runtime and the
EfficientSAM encoder/decoder as described above. Then, **after reviewing and
accepting BRIA's terms** for your use, from PowerShell in the repository run:

```powershell
.\tools\setup-bria.ps1 -AcceptBriaTerms
```

The script downloads BRIA's official quantized ONNX file pinned to revision
`36193ca99011a46a75e1862696b5a5a30ca25527`, checks SHA-256
`a6648479275dfd0ede0f3a8abc20aa5c437b394681b05e5af6d268250aaf40f3`, and
installs `models\bria_rmbg14_quantized.onnx` without overwriting existing files.
If the download fails due to access/network restrictions, follow the official
model page's conditions, download `onnx/model_quantized.onnx`, and run
`.\tools\setup-bria.ps1 -AcceptBriaTerms -ModelFile "C:\path\model_quantized.onnx"`.
The model is kept in the workspace, not the bundle or source repository.

In the Inspector turn on **AI Box: BRIA Detail (non-commercial, experimental)**,
then use **ADD BOX** and **REMOVE BOX** in the viewer. The old **DETAIL ON**
button remains the EfficientSAM alternate-mask option and does not alter BRIA add
boxes. Toggle BRIA off to return to the existing selector without changing the
saved boxes. If BRIA, EfficientSAM, or runtime is missing or inference fails, the
original image passes through rather than silently switching algorithms; see
`log.txt`, then use **Retry AI Load** or restart Resolve after installing the files.

BRIA and EfficientSAM run on CPU; GPU acceleration is not implemented. Each add
box invokes BRIA and each changed frame must be processed again. The full result is
cached for repeated renders with unchanged pixels and boxes. On the development
sandbox the BRIA prototype alone took roughly 2.8–3.5 seconds per box and reached
about 1.4 GiB peak RSS. A subsequent combined C++ selector test on the 1024×574
tree image peaked at about 2.3 GiB RSS in the sandbox; that test includes multiple
inferences, not a playback benchmark. Neither measurement is a laptop benchmark.
Resolve and multiple effects add memory use. Real Windows/Resolve,
render speed, timeline behavior, zoom/proxy alignment, and laptop memory usage
remain to be verified; save a project copy before trying this experimental mode.

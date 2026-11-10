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

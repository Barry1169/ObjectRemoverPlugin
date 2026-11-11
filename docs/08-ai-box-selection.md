# Experimental AI prompt selection inside Resolve

This is a separate, prompt-based selector, not GIMP/GEGL and not a brush. Give it
prompts and the EfficientSAM-Ti model predicts an object mask:

- **Box prompt:** drag a rough rectangle around an object. The model looks inside and
  near the rectangle.
- **Point prompt:** click once on the object. The model returns the object it finds
  under that click, which is often the whole subject but can be a part of it.

Add more prompts to union their predictions; remove prompts subtract predictions.
It is not guaranteed to select the intended object, can miss thin branches or poles,
and may include nearby sky. Prompts are hints, not clipping rectangles. It does not
track moving objects.

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
  then drag a loose rectangle around the object and release; or click **ADD POINT**
  and click once on the object. The first add prompt activates **AI prompt
  selection** and runs EfficientSAM; there is no U2NetP/key base matte in this mode.
  No external editor or file is needed. Until a valid add prompt exists, including
  after clearing all prompts, the image remains unchanged.
- The viewer toolbar has labelled **ADD BOX**, **REMOVE BOX**, **ADD POINT**,
  **REMOVE POINT**, **DETAIL OFF/ON**, **UNDO**, **CLEAR**, and **OFF** buttons in
  two rows. The yellow border marks a selected tool or enabled detail recovery.
  **OFF** stops drawing but does not disable the effect. The bar is positioned
  relative to the viewport when the host supplies compatible OpenGL matrices;
  otherwise it falls back to the source image corner. If it is not visible, zoom
  out/fit, enable Open FX Overlay, or use the Inspector controls.
- Additional add prompts union predictions; remove prompts subtract predictions in
  order. A prompt is not a clipping boundary. Prompts disappear on release. **UNDO**
  removes the last action; **CLEAR** deletes all saved actions including old strokes.
  Boxes and points share a limit of 32 prompts. Old manual/guided brush tools remain
  in the Inspector.
- **Remove point** is not a "this pixel is background" hint: it runs the model at
  that point and *subtracts* the object it finds, exactly like **REMOVE BOX**. Use it
  to cut a wrongly included area out of the selection, not to nudge an edge.
- Invert Matte (Output) flips the finished selection once, after all prompts, so the
  background becomes the subject. It is applied by the compositor, not by the
  selector.
- Output controls (Matte Only, Foreground Only, Background Only, Premultiply) apply to
  prompt-based selection. Matte Bias and the Detail/Feather/Expansion/Smoothing
  refinement controls do not: prompts define the edge and those stages are not
  implemented in this mode.
- **DETAIL ON** (also **AI Prompts: Recover More Detail** in the Inspector) tries the
  closest alternate AI mask for each add **box** and unions it with the best
  prediction; point prompts are unchanged. On the supplied tree test image it
  recovers part of the missing left foliage, but may retain sky or other unwanted
  areas and is not guaranteed to recover thin branches. It is off by default and can
  be switched off without changing prompts.
- Inspect Output → Matte Only or Foreground Only. Check zoom, project save/reopen,
  proxy/full-resolution alignment and final output in Resolve. The revised toolbar,
  the point prompts and detail mode have not yet been tested in Windows Resolve.

The model runs on CPU. The embedding and the finished matte are cached per effect
instance and reused while the input pixels and prompts remain unchanged; a cache hit
costs a hash of the frame plus a copy and composite of the matte (measured at about
15 ms for 1080p and 60 ms for UHD on one test machine, down from 69 ms and 280 ms),
while changing frames requires re-encoding. The encoder itself always rescales its
input to a square working resolution, so a wide frame is stretched inside the model;
the returned mask is mapped back to the frame, so the geometry stays correct. Model
output is at most 1024 pixels on its long edge and edge-aware upscaled. It has no
motion tracking. If the model/runtime is missing or inference fails, box mode passes
the original frame through unchanged and records the error in `log.txt`; **Retry AI
Load** retries loading on the next render. The experimental detail mode can produce
false positives or fail to recover branches.

## Video, playback and speed

A still image is the easy case. Every frame of a still clip has the same pixels, so
after the first frame the matte is a cache hit and the model never runs again. A
video changes pixels on every frame, so the cache misses and the encoder and decoder
run again for each one. That is why the same prompt that feels instant on a photo
makes playback crawl: the cost is not the drawing or the prompts, it is one CPU
inference per frame, and on CPU that inference is far slower than a video frame.

Three settings reduce it:

- **AI Prompts: Recompute** (Inspector, *Refine* group) decides what playback does.
  **Every frame (accurate)** recomputes on every frame and follows motion exactly; it
  is the default and the right choice for a final render. **Reuse while playing
  (smooth)** keeps the last computed selection while the host is in interactive
  playback or draft mode, so playback only pays the matte copy and the composite, and
  the first frame after playback stops is recomputed. **Once per prompt change
  (stills)** never follows motion and recomputes only when a prompt or the input
  changes, which suits stills and locked-off shots. Any change to the prompt set
  always invalidates the reuse, in every mode.
- **CPU Threads** (Inspector, *Performance* group) sets the ONNX intra-op thread
  count. `0` means automatic: every core, capped at 8 so Resolve keeps some CPU.
  Earlier builds hardcoded 2 threads whatever the machine, so an 8- or 16-core CPU
  sat mostly idle. Changing the value reloads the model on the next render.
- **Fewer prompts first.** One loose box is usually enough. Each extra prompt is
  another decoder pass, and detail recovery adds extra full-resolution work for every
  add box.

Measured on the supplied model on a two-core test machine at 720p: about 1.6 s per
frame with **Every frame**, against about 5 ms per frame with either reuse policy
after the first frame. That is the difference between roughly half a frame per second
and real-time playback, and it is why a still looked fine while a video did not.
Reused frames keep the previous selection, so a moving subject visibly lags its
outline during playback; that lag is the price of smooth playback, not a bug. Switch
back to **Every frame** before judging accuracy or rendering.

The legacy automatic U2NetP/key modes are still available explicitly under Viewer
Selection Method; the **Automatic Model (legacy)** setting applies only to those
modes. Its value does not make U2NetP run in Ready or AI prompt selection mode.
Existing saved explicit choice indices remain unchanged, but some hosts may omit
parameters saved at their old defaults and use new defaults when reloading a project.
Back up projects and verify existing instances after updating.

Projects containing boxes use the `AIBGR4` storage format. A project that also
contains point prompts uses `AIBGR5`, because older builds cannot express a
single-point prompt; older builds reject the whole stored stroke list rather than
applying part of it, so keep a separate backup before downgrading. Existing project
parameter names and old choice indices remain unchanged.

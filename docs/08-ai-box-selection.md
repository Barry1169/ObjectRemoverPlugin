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
- Click **A** (add box) at the right end of the on-screen toolbar, or choose
  **AI add box** in Viewer Brush. Drag a loose rectangle around the whole desired
  object and release. This automatically switches Viewer Selection Method to
  **AI box selection**. The result replaces the AI/key matte; existing brush
  strokes are not used in this mode. No external GIMP, PNG or mask file is needed.
- Draw further **A** boxes around missed objects to add them. **D** draws a box
  whose predicted object is removed from the current selection. These operations
  are applied in order. Boxes do not remain drawn after release. **U** undoes the
  last saved action; **X** clears all saved actions, including older brush strokes.
  Up to 32 boxes are accepted.
- Inspect Output → Matte Only or Foreground Only. Use a disposable project and
  check save/reopen, zoom, proxy/full-resolution alignment and final output. This
  interaction and model have not yet been tested in Windows Resolve. Different
  bounding boxes or overlapping objects can produce very different masks.

The model runs on CPU. The image embedding is cached per effect instance and is
reused while the input pixels remain unchanged; changing frames or input pixels
requires re-encoding. Model output is at a maximum 1024-pixel long edge and is
edge-aware upscaled. It has no temporal tracking. If the model or runtime is
missing or inference fails, the existing AI/key matte is used and the error is in
`log.txt`. `Retry AI Load` retries missing models on the next render. The tool is
not a promptable matting engine and cannot promise hair-thin detail or perfect
foreground/background separation.

Projects containing boxes use a new `AIBGR4` storage format. Older builds cannot
read it, so keep a separate backup before downgrading. Existing project parameter
names and old choice indices remain unchanged.

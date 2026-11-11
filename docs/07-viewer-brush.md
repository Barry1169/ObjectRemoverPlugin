# Experimental in-Resolve guided selection

The viewer tools work entirely inside Resolve. No GIMP, PNG or external editor is needed.
**Guided selection is an experimental colour-and-edge classifier, not GIMP/GEGL's Foreground
Select or an equivalent matting engine.** It uses your outline and markings to infer unmarked
regions, but it can confuse similar colours, produce soft or rough edges, and miss thin branches.
It needs testing on the actual image; do not assume it will recover the whole tree.

## Trying the selection

1. Back up the project and plugin bundle, close Resolve before installing, then test in a disposable
   project. To use this *legacy guided* method, choose **Automatic + guided selection** under
   Viewer Selection Method and select a model under **Automatic Model (legacy)**. The new default
   Ready mode does not apply an automatic matte.
2. Select the effect and enable **Open FX Overlay**. The labelled toolbar now focuses on AI boxes;
   choose **Outline (guided)**, **Keep**, or **Remove** in the Inspector's Viewer Brush menu for
   the legacy guided tool. The Inspector also has the radius, Undo and Clear controls.
3. Under **Guided Brush Behaviour**, choose **Samples only** to guide the selection without solid
   full-resolution paint, or use **Direct corrections + samples** for the original behavior.
   Draw a loose outline around the subject, then broad Keep/Remove marks for representative colours.
   This older colour classifier may still miss details and is not GIMP foreground selection.
4. Strokes appear only while dragging; the saved selection persists. Inspect Output → Matte Only,
   and verify saving/reopening, proxy/full-resolution alignment and final output.

## What it does and limitations

- In Guided mode, painted samples and the outside of the most recent outline build approximate
  foreground/background colour models. A bounded 320-pixel-long-edge solver combines those models,
  the AI/key matte and local colour edges to estimate unmarked pixels. Upsampling uses the original
  image's colour edges. In Direct mode, painted corrections are then applied at full resolution;
  Samples only skips that solid patch. This is **not** GIMP's matting solver, and is not a
  full-resolution or semantic object selector. Similar-coloured foreground and background, tiny
  branches, textures and transparency are especially challenging. It can alter areas far from a
  stroke. Always check the matte and final render.
- The outline is straight-line connected, automatically closed and sampled roughly every 10
  canonical pixels. Keep it loose and include everything you want. Red and green brush strokes
  override the outline locally. A new outline replaces the previous one for selection, but undo
  restores the earlier one.
- The combined limit is now 8,192 points and the per-stroke limit is 4,096 points. Newly drawn
  strokes are simplified within a small geometric tolerance. Strokes over either limit are
  discarded and logged; the host is also sent a warning when available. Use shorter strokes,
  Undo or Clear if needed. Complex strokes and colour-aware upsampling add CPU render time.
- Marks are static image coordinates, **not motion tracking** or temporal propagation. The model
  runs on CPU per frame; guided selection adds CPU work. Results may differ on proxies and exports.
- Existing saved explicit manual/guided indices and strokes are still read, but hosts may use the
  new Ready default for parameters omitted from old projects. Verify old instances after updating. Outline data, and projects with more than 512 stored points,
  use newer formats which older plugin builds cannot read; save a separate project copy before
  downgrading. Guided results can change because the edge-aware upsampling has been improved.
- Overlay drawing/interaction depends on Resolve page and viewer. If no controls appear, check the
  log for `viewer overlay: OpenGL context unavailable`, `viewer brush overlay registered`,
  `viewer brush: started` and `committed`. Inspector controls remain available. Undo and Clear
  affect stored data, not merely the visible overlay.

# Experimental in-Resolve guided selection

The viewer tools work entirely inside Resolve. No GIMP, PNG or external editor is needed.
**Guided selection is an experimental colour-and-edge classifier, not GIMP/GEGL's Foreground
Select or an equivalent matting engine.** It uses your outline and markings to infer unmarked
regions, but it can confuse similar colours, produce soft or rough edges, and miss thin branches.
It needs testing on the actual image; do not assume it will recover the whole tree.

## Trying the selection

1. Back up the project and plugin bundle, close Resolve before installing, then test in a disposable
   project. Choose **U2NetP (AI, CPU)** for the starting matte. The Guidance PNG field may stay empty.
2. Select the effect and enable **Open FX Overlay** in the viewer. A small toolbar should appear
   near the lower-left corner of the source image. It has a dark background and larger icons;
   actual appearance depends on the host's OpenGL context. Its nine buttons are **O L K R G − + U X**:
   - **O**: stop drawing. **L**: rough outline and turn on guided selection. **K**: green keep
     brush. **R**: red remove brush. **G**: toggle guided/manual selection.
   - **− / +**: reduce/increase brush radius. **U**: undo last stroke/outline. **X**: clear all.
     The Inspector retains the corresponding controls and shows the actual brush radius and
     selection method. Toolbar controls require the host to provide a source region and viewer
     pixel scale; use Inspector controls if the toolbar is not shown.
3. Choose **L**. Drag one continuous rough loop around the entire tree, branches, buildings and
   ground, leaving a margin; release to close it automatically. The latest outline is used. Its
   outside supplies background samples. Then choose **K**, and mark representative foreground
   colours, including branches, foliage, buildings and ground. If sky remains inside the outline,
   choose **R** and mark representative sky colours. Start small and add strokes as needed. A
   selection needs both foreground and background samples; until then the starting matte remains,
   with ordinary painted corrections applied. The outline has no effect in Manual mode.
4. Strokes/outline appear while dragging, then disappear on release; the saved corrections and
   selection remain active. Wait for the CPU re-render. Choose **Output → Matte Only** to inspect the
   result. Use **U** or **X** for mistakes and **O** to stop drawing. Switch Open FX Overlay off to
   hide the toolbar too; it should not disable the matte.
5. Verify saving/reopening, proxy/full-resolution alignment and the final export before relying on
   it. The earlier direct brush has been exercised in Resolve 21; this new toolbar and guided
   selector are experimental; the toolbar and marking input have been exercised in Resolve 21,
   but selection quality, persistence and final export still require verification.

## What it does and limitations

- In Guided mode, painted samples and the outside of the most recent outline build approximate
  foreground/background colour models. A bounded 320-pixel-long-edge solver combines those models,
  the AI/key matte and local colour edges to estimate unmarked pixels. Direct painted corrections
  are then applied at full resolution. This is **not** GIMP's matting solver, and is not a
  full-resolution or semantic object selector. Similar-coloured foreground and background, tiny
  branches, textures and transparency are especially challenging. It can alter areas far from a
  stroke. Always check the matte and final render.
- The outline is straight-line connected, automatically closed and sampled roughly every 10
  canonical pixels. Keep it loose and include everything you want. Red and green brush strokes
  override the outline locally. A new outline replaces the previous one for selection, but undo
  restores the earlier one.
- The combined limit remains 512 sampled points; if a new stroke exceeds the limit it is discarded
  and logged. A warning is also sent to the host when available, though Resolve may not display it.
  Use fewer/shorter strokes, Undo or Clear. Errors are in the plugin log.
- Marks are static image coordinates, **not motion tracking** or temporal propagation. The model
  runs on CPU per frame; guided selection adds CPU work. Results may differ on proxies and exports.
- Existing projects default to **Manual corrections** and retain their old brush behavior. The
  plugin reads old saved strokes. A project containing a new outline uses a newer stroke-data format
  which older plugin builds cannot read; save a separate project copy before downgrading.
- Overlay drawing/interaction depends on Resolve page and viewer. If no controls appear, check the
  log for `viewer overlay: OpenGL context unavailable`, `viewer brush overlay registered`,
  `viewer brush: started` and `committed`. Inspector controls remain available. Undo and Clear
  affect stored data, not merely the visible overlay.

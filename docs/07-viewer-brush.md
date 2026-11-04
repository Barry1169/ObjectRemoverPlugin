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
3. Under **Matte Refinement → Guided Brush Behaviour**, choose **Samples only** to try
   guiding the selection without the original full-resolution solid paint. It takes effect only
   in Guided mode. The default **Direct corrections + samples** preserves existing projects.
   Switching it on reinterprets existing marks as samples, which can change the result. Samples
   are still areas under the brush, not precise object clicks: keep them inside the object.
   Choose **L**. Drag one continuous rough loop around the entire tree, branches, buildings and
   ground, leaving a margin; release to close it automatically. The latest outline is used. Its
   outside supplies background samples. Then choose **K**, and mark representative foreground
   colours, including branches, foliage, buildings and ground. If sky remains inside the outline,
   choose **R** and mark representative sky colours. Start small and add strokes as needed. A
   selection needs both foreground and background samples; until then the starting matte remains.
   In the default Direct setting ordinary paint still applies; in Samples only it does not.
   The outline has no effect in Manual mode.
4. Strokes/outline appear while dragging, then disappear on release; the saved corrections and
   selection remain active. Wait for the CPU re-render. Choose **Output → Matte Only** to inspect the
   result. Use **U** or **X** for mistakes and **O** to stop drawing. Switch Open FX Overlay off to
   hide the toolbar too; it should not disable the matte.
5. Verify saving/reopening, proxy/full-resolution alignment and the final export before relying on
   it. The earlier toolbar and marking input were exercised in Resolve 21, but the newer toolbar,
   samples-only mode, edge-aware upsampling, persistence and final export require real-host testing.

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
- Existing projects default to **Manual corrections** and retain their old brush behavior. The
  plugin reads old saved strokes. Outline data, and projects with more than 512 stored points,
  use newer formats which older plugin builds cannot read; save a separate project copy before
  downgrading. Guided results can change because the edge-aware upsampling has been improved.
- Overlay drawing/interaction depends on Resolve page and viewer. If no controls appear, check the
  log for `viewer overlay: OpenGL context unavailable`, `viewer brush overlay registered`,
  `viewer brush: started` and `committed`. Inspector controls remain available. Undo and Clear
  affect stored data, not merely the visible overlay.

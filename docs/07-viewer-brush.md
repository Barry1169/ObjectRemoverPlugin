# Experimental in-Resolve viewer brush

This first prototype lets you paint **direct matte corrections in Resolve**. It does not require
GIMP, a PNG or any other application. **It is not yet GIMP's Foreground Select or an automatic
edge-following algorithm**: paint changes only the pixels touched. Unpainted pixels keep the AI/key
matte. This is a step toward an interactive selection workflow, not a promise that one stroke will
select the whole tree or that fine edges are solved.

## Try it on a still image

1. Back up your project and plugin bundle, close Resolve and install this build. Restart Resolve and
   use a disposable test project; this new interaction has not been tested in Windows/Resolve yet.
2. Apply AI Background Remover. Choose **U2NetP (AI, CPU)** for the starting matte, and open
   **Matte Refinement**. Leave the optional Guidance PNG field empty.
3. In the viewer's on-screen-controls menu, select **Open FX Overlay**. Select this effect in the
   Inspector. Exact menu availability can vary by Resolve page and version.
4. Set **Viewer Brush (experimental)** to **Keep (green)**. Adjust **Viewer Brush Radius**, then
   drag over a small area of the tree that was incorrectly removed. Release the mouse and wait for
   the CPU AI to re-render. The painted pixels should become visible. Use **Remove (red)** for sky
   that AI mistakenly kept. Painting over sky in Keep mode keeps sky too; stay inside the desired
   region. Soft edges are a narrow brush feather, not inferred object boundaries.
5. Use **Undo Last Brush Stroke** or **Clear Viewer Brush Strokes** to correct mistakes. Set Viewer
   Brush to **Off** to stop intercepting mouse clicks. To hide the visual stroke outlines, switch the
   viewer away from Open FX Overlay; this should not disable the matte correction.
6. Select **Output → Matte Only** to check the mask. Test saving, closing and reopening the project
   before relying on it: strokes are stored in an OFX parameter, but persistence has only been tested
   in a fake host. Also test the final full-resolution render.

## Limitations and troubleshooting

- A host reporting `overlays=1` does not prove a usable drawing context or pen events. This prototype
  registers the OFX 1.4 overlay interface and attempts to draw using the host's OpenGL context. If
  Resolve does not supply one, outlines will not appear; check the log for `viewer overlay: OpenGL
  context unavailable`, `viewer brush overlay registered`, `viewer brush: started` and `committed`.
  Resolve may also not display the controls in every page or viewer. This must be tested on the
  actual target machine; the fake host cannot prove mouse or graphics integration.
- Brush strokes are static in image coordinates and do **not** track a moving subject or camera.
  Image framing/transforms and host behavior may affect alignment; check both preview and export.
- The combined stroke limit is 512 sampled points. If reached, use Undo or Clear. Big brushes and
  many long strokes are slower; AI still runs on CPU per render.
- Brush mode Off does not erase strokes. Existing saved projects start with no strokes and Off.
  The PNG guidance feature remains available for compatibility, but is not needed here.
- The prototype has no smart object selection, GIMP/GEGL matting, temporal propagation, or GPU
  acceleration. Those require separate implementation, licensing and real-host verification.

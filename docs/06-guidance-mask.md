# Manual guidance PNG for the matte

This is a **manual correction** workflow for stills or stationary shots. It is not GIMP's matting
algorithm: coloured paint directly overrides the existing AI or colour-key matte at painted pixels;
transparent areas keep the original matte. No GIMP source code is included. The matte cannot track a
moving camera or subject. The new feature has not yet been tested in Windows or Resolve.

## Paint corrections in GIMP

1. Use an image that exactly matches the full rendered frame and framing in Resolve. For the user's
   log this is 1920 × 1080; verify your own frame size. **The PNG must match the frame's exact pixel
   dimensions**, and the tree must line up pixel-for-pixel with the Resolve frame. If an original
   still is smaller, scaled, cropped or transformed in Resolve, use a reference exported from the
   uncorrected Resolve frame instead. Don't resize or overwrite the original file.
2. Open that reference in GIMP. Create a **new, full-canvas transparent layer** above it. Use this
   layer for all corrections. Keep the original reference visible temporarily as a painting guide.
3. Use an opaque brush at 100% opacity on the new layer. Paint **pure green (`#00ff00`)** over
   portions of the tree/buildings/ground that should be kept but AI missed. Paint **pure red
   (`#ff0000`)** over sky or background that AI kept but should remove. Paint only where you want a
   correction: every green pixel forces the original picture to be kept, so green on the sky will
   also keep sky. Red removes whatever it covers. Soft/partially transparent strokes blend instead
   of forcing; neutral colours are ignored. The algorithm does not automatically follow edges or
   infer areas between strokes.
4. Hide the reference layer. You should see only red/green strokes over checkerboard transparency.
   Save an editable `.xcf` copy separately. Use **File → Export As** to export the visible guidance
   layer as a **new PNG with transparency**, e.g. `D:\AI-Effects\ObjectRemoverPlugin-Fix\tree-guide.png`.
   Do not export with the original reference visible or the transparent areas will become opaque.
5. In Resolve, keep **Model Selection → U2NetP (AI, CPU)** for the base matte. Under **Matte
   Refinement**, choose the file in **Guidance PNG (green keep, red remove)**. Use **Output → Matte
   Only** to inspect. Use Foreground Only or Composite to see the cutout. To compare, temporarily
   clear the guidance path; it does not modify your source image or model file.
6. After editing and re-exporting the PNG, click **Reload Guidance PNG**, then move the playhead if
   Resolve does not immediately render again. The file is also checked for changes on render.
   Avoid editing a mask during final export; file changes can cause frame-to-frame inconsistency.

The PNG is read locally with a limit of 128 MB and 40 million pixels. An absent, invalid, opaque
(no-alpha), mismatched-size or unreadable PNG is ignored; the original AI/key matte is preserved and
an explanation is logged to the normal log. Transparent regions and neutral paint have no effect.
The mask applies before compositing and after Matte Bias. The default output is Composite with Matte
Invert off; Background Only deliberately displays the opposite portion of the matte.

At proxy/render scales or when the host supplies a differently sized image/window, the PNG is ignored
rather than being stretched/misaligned. If preview looks unchanged, check the full-resolution render,
path, alpha, frame size and `log.txt`. Changing transforms or source dimensions requires repainting
or realigning the mask. Existing projects with an empty guidance path render as before.

## License and provenance

This plugin's correction logic is project code; no GIMP or GEGL algorithm is copied. PNG decoding
uses `stb_image` under its MIT license (source pinned in `third_party/stb`); the bundle contains the
license notice. GIMP itself is GPLv3-or-later; using GIMP to paint a separate image does not mean its
source code is embedded here. Existing AI model-weight licensing caveats still apply.

// SPDX-License-Identifier: MIT
#pragma once

#include "core/FloatImage.h"
#include "core/PipelineSettings.h"
#include "matte/Matte.h"

namespace aibgr {

/// Compositing layer: turns (original straight RGBA, refined alpha) into the output the inspector
/// asked for.
///
/// Contract:
///   * `srcStraight` is **un-premultiplied**, in the host's own colour encoding. It is never
///     colour-transformed, which is why this plugin cannot introduce a colour shift: the RGB that
///     leaves here is the RGB that came in, scaled only by alpha.
///   * `alpha` must have the same dimensions as `srcStraight` and hold values in [0, 1].
///   * `dst` is written **un-premultiplied**. Re-premultiplying to match the declared output
///     happens in the plugin layer, once, at write-out.
///
/// Mode semantics. Let `a` be the matte and `bgRetain = 1 - backgroundTransparency`:
///
///   Composite        RGB = src            A = a + bgRetain * (1 - a)
///   Foreground Only  RGB = src            A = a
///   Background Only  RGB = src            A = (1 - a)
///   Matte Only       RGB = m, A = 1       m = matteInvert ? 1 - a : a
///
/// `backgroundTransparency` is how transparent the background BECOMES, so bgRetain = 1 - it is the
/// alpha handed to background pixels in Composite mode. The default of 1.0 fully removes the
/// background, which is what the plugin is for; 0.0 leaves the frame untouched and is what makes
/// isPassThrough() true. Background Only ignores it entirely: that mode exists to preview what was
/// removed, and an empty preview would defeat the purpose. Foreground Only and Matte Only ignore it
/// too.
void composite(const FloatImage& srcStraight, const Matte& alpha, const PipelineSettings& settings,
               FloatImage& dst);

/// True if, for these settings, the output equals the input pixel-for-pixel. Used by isIdentity()
/// so Resolve can skip the node entirely instead of paying for a copy.
bool isPassThrough(const PipelineSettings& settings) noexcept;

} // namespace aibgr

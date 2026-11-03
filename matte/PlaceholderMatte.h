// SPDX-License-Identifier: MIT
#pragma once

#include "core/FloatImage.h"
#include "matte/Matte.h"

namespace aibgr {

/// The fallback matte source used when no AI model is available.
///
/// This exists because of the requirement "handle missing models gracefully" and
/// "correct behavior when AI models are unavailable". Passing the image straight through is
/// *safe*, but it is also useless. A border-colour key is only ~40 lines and gives the plugin a
/// genuinely working basic mode with no model installed, while every downstream stage
/// (refinement, temporal, compositing) still gets exercised.
///
/// It is a chroma/luma distance key against the median colour of the frame border, not a
/// segmentation network. It works on a subject against a reasonably uniform background and fails
/// on a busy one. The Diagnostics status says so explicitly.
struct FallbackKeySettings {
    /// Normalised RGB distance from the estimated background colour at which a pixel is 50%
    /// foreground. Behaves like a keyer's tolerance: LOW keeps more foreground (only an exact
    /// colour match counts as background), HIGH keys more away (the band moves out towards the
    /// maximum possible colour distance of 1.0, so almost everything reads as background).
    float tolerance = 0.25f;
    /// Width of the soft transition around `tolerance`, in the same normalised units.
    float softness = 0.08f;
    /// How many pixels in from each edge to sample when estimating the background colour.
    int border = 4;
};

struct BorderColour {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    int samples = 0;
};

/// Median colour of a `border`-pixel frame around the image edges. The median rather than the mean
/// so that a subject touching one edge does not drag the estimate.
BorderColour estimateBorderColour(const FloatImage& image, int border = 4) noexcept;

/// **Squared** normalised RGB distance, in [0, 1]: 0 for identical colours, 1 for black vs white.
///
/// Deliberately squared (mean of the squared channel deltas, so the sqrt cancels): it is monotonic
/// in the true distance, which is all a threshold needs, and it avoids a per-pixel sqrt in the
/// innermost loop of the fallback path. Callers that compare against a linear tolerance must square
/// the tolerance first, which computeFallbackKeyMatte() does.
inline float colourDistanceSquared(float r0, float g0, float b0, float r1, float g1,
                                   float b1) noexcept {
    const float dr = r0 - r1;
    const float dg = g0 - g1;
    const float db = b0 - b1;
    return (dr * dr + dg * dg + db * db) * (1.0f / 3.0f);
}

/// Writes a matte the same size as `image`.
void computeFallbackKeyMatte(const FloatImage& image, Matte& out,
                             const FallbackKeySettings& settings) noexcept;

} // namespace aibgr

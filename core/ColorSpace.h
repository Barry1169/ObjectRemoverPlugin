// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include "core/FloatImage.h"

namespace aibgr {

/// What the incoming pixels mean, for the purpose of feeding a segmentation network.
///
/// IMPORTANT: this transform is applied **only to the scratch buffer handed to the network**.
/// The composited output is always derived from the untouched original pixels, so it is
/// impossible for this choice to introduce a colour shift in the rendered image. Getting it
/// wrong degrades the matte; it never changes your colours. That is the whole reason the
/// transform is confined to the inference path.
enum class ColorSpaceAssumption : uint8_t {
    Auto = 0,        ///< heuristic, see autoDetect()
    DisplayReferred, ///< sRGB / Rec.709 / DaVinci YRGB on a Rec.709 timeline. Identity.
    SceneLinear,     ///< linear light. Encoded to sRGB before inference.
    LogC,            ///< ARRI LogC (EI 800). Decoded to linear, then encoded to sRGB.
};

const char* colorSpaceName(ColorSpaceAssumption c) noexcept;

/// Segmentation and matting networks are trained on ordinary images as loaded by PIL/OpenCV:
/// **sRGB-encoded**, 0..1, then normalised with an ImageNet mean/std. So whatever the host hands
/// us must arrive at the network in sRGB encoding. These are the transfer functions.
float srgbEncode(float linear) noexcept;
float srgbDecode(float srgb) noexcept;

/// ARRI LogC (EI 800) -> linear. Standard published constants.
float logCToLinear(float logC) noexcept;

/// Heuristic used by ColorSpaceAssumption::Auto.
///
/// Scene-linear images of ordinary content have a markedly lower mean luminance than the same
/// images sRGB-encoded (roughly 0.05-0.20 versus 0.30-0.55), so mean luminance separates them
/// well enough to pick a default. This is a heuristic, not a measurement: it will misfire on a
/// very dark or very bright shot. That is acceptable precisely because the result only affects
/// the network's input, and the inspector offers a manual override one click away.
ColorSpaceAssumption autoDetect(const FloatImage& image) noexcept;

/// Convert an RGBA float image, in place, from `assumption` into the sRGB-encoded form a
/// segmentation network expects. Alpha is never touched.
void toInferenceEncoding(FloatImage& image, ColorSpaceAssumption assumption) noexcept;

/// Mean luminance using Rec.709 coefficients, ignoring alpha. Exposed for the heuristic and for
/// tests.
double meanLuminance(const FloatImage& image) noexcept;

} // namespace aibgr

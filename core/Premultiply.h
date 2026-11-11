// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include "core/FloatImage.h"

namespace aibgr {

/// Mirrors kOfxImageEffectPropPreMultiplication (ofxImageEffect.h:1155-1200).
enum class Premultiplication : uint8_t {
    Unknown = 0,
    Premultiplied,
    Straight,
};

const char* premultiplicationName(Premultiplication p) noexcept;

/// Default alpha epsilon. Below this a straight-color value cannot be recovered from a
/// premultiplied one, because the division would explode; see unpremultiply().
constexpr float kAlphaEpsilon = 1e-4f;

/// rgb *= a, in place, over `pixelCount` RGBA pixels.
void premultiplyRow(float* rgba, int pixelCount) noexcept;

/// Recover straight colour from premultiplied, in place, over `pixelCount` RGBA pixels.
///
/// Where alpha <= epsilon the original colour is genuinely unrecoverable (it was multiplied by
/// ~0 and thrown away), so rgb is set to 0 rather than dividing by ~0 and producing garbage that
/// would confuse the segmentation network. This is the single most common cause of a bright halo
/// around a keyed subject, so it is handled explicitly and tested.
void unpremultiplyRow(float* rgba, int pixelCount, float alphaEpsilon = kAlphaEpsilon) noexcept;

void premultiply(FloatImage& image);
void unpremultiply(FloatImage& image, float alphaEpsilon = kAlphaEpsilon);

/// True if the image is already premultiplied within tolerance, i.e. no colour component exceeds
/// its alpha anywhere by more than `tolerance`. Used by the `Auto` premultiplication path and by
/// tests. Note this is a heuristic: an opaque image satisfies it trivially, and so does any
/// straight image whose colours happen to be <= alpha.
bool looksPremultiplied(const FloatImage& image, float tolerance = 1e-3f) noexcept;

} // namespace aibgr

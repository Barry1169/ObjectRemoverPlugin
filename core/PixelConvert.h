// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"

#include <cstdint>

namespace aibgr {

/// IEEE-754 binary16 <-> binary32.
///
/// Hand-rolled rather than pulled from ofxPixels.h (which defines no conversion) or from a
/// dependency. Handles sign, zero, denormals, infinities and NaN. `floatToHalf` rounds to
/// nearest-even and saturates overflow to +/-Inf, matching what a compositor expects.
float halfToFloat(uint16_t h) noexcept;
uint16_t floatToHalf(float f) noexcept;

/// Convert `count` consecutive components from `src` (format `depth`) into float32 `dst`,
/// normalised so that the integer depths map 0 -> 0.0 and their maximum -> 1.0.
///
/// `src` and `dst` must not overlap. No bounds checking: the caller passes a row length it
/// derived from the image geometry, and this is the innermost loop of the plugin.
void componentsToFloat(const void* src, BitDepth depth, float* dst, int count) noexcept;

/// Inverse of componentsToFloat. Integer depths round to nearest and clamp; float depths are
/// copied verbatim (no clamping - a compositor must be able to carry values above 1.0).
void componentsFromFloat(const float* src, void* dst, BitDepth depth, int count) noexcept;

/// Convenience: convert one interleaved RGBA pixel.
void pixelToFloat(const void* src, BitDepth depth, float outRGBA[4]) noexcept;
void pixelFromFloat(const float inRGBA[4], void* dst, BitDepth depth) noexcept;

} // namespace aibgr

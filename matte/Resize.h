// SPDX-License-Identifier: MIT
#pragma once

#include "core/FloatImage.h"
#include "matte/Matte.h"

namespace aibgr {

/// Resampling for mattes and images.
///
/// Downscaling uses a proper area average, not point sampling: point-sampling a 4K frame down to
/// 512 throws away 98% of the information and aliases hair into noise. Upscaling uses separable
/// Catmull-Rom, which keeps an edge crisper than bilinear without the ringing of a sharper kernel,
/// and clamps to [0,1] so a matte can never overshoot into negative alpha.

/// Area-average downscale. If dst is larger than src this falls back to bilinear.
void resizeArea(const Matte& src, Matte& dst);

/// Separable Catmull-Rom bicubic, output clamped to [0, 1].
void resizeBicubic(const Matte& src, Matte& dst);

/// Separable bilinear. Provided mainly so tests can compare kernels.
void resizeBilinear(const Matte& src, Matte& dst);

/// Area-average downscale for RGBA images. Alpha is averaged like any other channel; the caller
/// decides whether the image is premultiplied at this point (averaging premultiplied RGBA is the
/// correct thing to do, which is why the pipeline downscales BEFORE un-premultiplying).
void resizeArea(const FloatImage& src, FloatImage& dst);

/// Compute the matte resolution to use for a frame whose long edge is `frameLongEdge`, given the
/// requested model resolution `requested`. Never upscales: a 400 px source does not benefit from
/// a 512 px matte, and paying for it on a 4 GB GPU would be pure waste.
int effectiveMatteResolution(int frameLongEdge, int requested) noexcept;

/// Fit (w, h) into a box of at most `maxEdge` on the long side, preserving aspect ratio, and round
/// both dimensions down to a multiple of `alignment` (models with a patchified or windowed
/// backbone require this; BiRefNet's Swin backbone needs a multiple of its window size).
/// Always returns at least `alignment` on each side.
void fitAndAlign(int width, int height, int maxEdge, int alignment, int& outW, int& outH) noexcept;

} // namespace aibgr

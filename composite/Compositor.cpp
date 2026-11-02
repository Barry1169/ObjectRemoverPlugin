// SPDX-License-Identifier: MIT
#include "composite/Compositor.h"

#include <algorithm>

namespace aibgr {

void composite(const FloatImage& srcStraight, const Matte& alpha, const PipelineSettings& settings,
               FloatImage& dst) {
    if (srcStraight.empty() || dst.empty()) {
        return;
    }
    if (dst.width() != srcStraight.width() || dst.height() != srcStraight.height()) {
        dst.resize(srcStraight.width(), srcStraight.height());
    }

    const float bgRetain = std::min(1.0f, std::max(0.0f, 1.0f - settings.backgroundTransparency));
    const bool invert = settings.matteInvert;
    const int w = srcStraight.width();
    const int h = srcStraight.height();

    // The matte is normally full-frame. If a caller hands us a mismatched one (a bug upstream, or
    // a deliberately cheap preview), fall back to nearest-neighbour indexing rather than reading
    // out of bounds.
    const bool sizeMatches = (alpha.width() == w && alpha.height() == h);

    switch (settings.outputMode) {
    case OutputMode::MatteOnly: {
        for (int y = 0; y < h; ++y) {
            float* d = dst.row(y);
            const float* a = sizeMatches ? alpha.row(y) : nullptr;
            for (int x = 0; x < w; ++x, d += 4) {
                float v = 0.0f;
                if (a != nullptr) {
                    v = a[x];
                } else if (!alpha.empty()) {
                    const int ax = std::min(alpha.width() - 1, x * alpha.width() / std::max(1, w));
                    const int ay = std::min(alpha.height() - 1, y * alpha.height() / std::max(1, h));
                    v = alpha.at(ax, ay);
                }
                if (invert) {
                    v = 1.0f - v;
                }
                v = std::min(1.0f, std::max(0.0f, v));
                d[0] = v;
                d[1] = v;
                d[2] = v;
                d[3] = 1.0f;
            }
        }
        return;
    }
    case OutputMode::Composite:
    case OutputMode::ForegroundOnly:
    case OutputMode::BackgroundOnly:
        break;
    }

    const bool foregroundMode = (settings.outputMode == OutputMode::ForegroundOnly);
    const bool backgroundMode = (settings.outputMode == OutputMode::BackgroundOnly);

    for (int y = 0; y < h; ++y) {
        const float* s = srcStraight.row(y);
        float* d = dst.row(y);
        const float* a = sizeMatches ? alpha.row(y) : nullptr;
        for (int x = 0; x < w; ++x, s += 4, d += 4) {
            float av;
            if (a != nullptr) {
                av = a[x];
            } else if (!alpha.empty()) {
                const int ax = std::min(alpha.width() - 1, x * alpha.width() / std::max(1, w));
                const int ay = std::min(alpha.height() - 1, y * alpha.height() / std::max(1, h));
                av = alpha.at(ax, ay);
            } else {
                av = 1.0f; // no matte at all: keep the image
            }
            av = std::min(1.0f, std::max(0.0f, av));
            if (invert) {
                av = 1.0f - av;
            }

            float outA;
            if (foregroundMode) {
                outA = av;
            } else if (backgroundMode) {
                // Background Only is a PREVIEW of what was removed, so it is the plain inverse
                // matte. Scaling it by the background transparency would make the default setting
                // show an empty frame, which is the opposite of what the mode is for.
                outA = 1.0f - av;
            } else { // Composite
                outA = av + bgRetain * (1.0f - av);
            }

            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = outA;
        }
    }
}

bool isPassThrough(const PipelineSettings& settings) noexcept {
    if (!settings.enable) {
        return true;
    }
    // backgroundTransparency is how transparent the background BECOMES, so 0 means "leave it
    // alone": Composite then writes A = a + 1 * (1 - a) = 1 with the source RGB, which is the input
    // pixel for pixel. At the default of 1 the background is fully removed and this is false.
    if (settings.outputMode == OutputMode::Composite && settings.backgroundTransparency <= 0.0f) {
        return true;
    }
    return false;
}

} // namespace aibgr

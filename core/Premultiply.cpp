// SPDX-License-Identifier: MIT
#include "core/Premultiply.h"

#include <algorithm>
#include <cmath>

namespace aibgr {

const char* premultiplicationName(Premultiplication p) noexcept {
    switch (p) {
    case Premultiplication::Unknown: return "Unknown";
    case Premultiplication::Premultiplied: return "Premultiplied";
    case Premultiplication::Straight: return "Straight";
    }
    return "?";
}

void premultiplyRow(float* rgba, int pixelCount) noexcept {
    for (int i = 0; i < pixelCount; ++i) {
        float* p = rgba + static_cast<std::size_t>(i) * 4u;
        const float a = p[3];
        p[0] *= a;
        p[1] *= a;
        p[2] *= a;
    }
}

void unpremultiplyRow(float* rgba, int pixelCount, float alphaEpsilon) noexcept {
    for (int i = 0; i < pixelCount; ++i) {
        float* p = rgba + static_cast<std::size_t>(i) * 4u;
        const float a = p[3];
        if (a > alphaEpsilon) {
            const float inv = 1.0f / a;
            p[0] *= inv;
            p[1] *= inv;
            p[2] *= inv;
        } else {
            p[0] = 0.0f;
            p[1] = 0.0f;
            p[2] = 0.0f;
        }
    }
}

void premultiply(FloatImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        premultiplyRow(image.row(y), image.width());
    }
}

void unpremultiply(FloatImage& image, float alphaEpsilon) {
    for (int y = 0; y < image.height(); ++y) {
        unpremultiplyRow(image.row(y), image.width(), alphaEpsilon);
    }
}

bool looksPremultiplied(const FloatImage& image, float tolerance) noexcept {
    for (int y = 0; y < image.height(); ++y) {
        const float* p = image.row(y);
        for (int x = 0; x < image.width(); ++x, p += 4) {
            const float a = p[3];
            // A straight (unpremultiplied) image can legitimately have rgb > a, e.g. a bright
            // subject over a 50% transparent background. That is the signature we look for.
            if (p[0] > a + tolerance || p[1] > a + tolerance || p[2] > a + tolerance) {
                return false;
            }
        }
    }
    return true;
}

} // namespace aibgr

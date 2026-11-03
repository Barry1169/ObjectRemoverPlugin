// SPDX-License-Identifier: MIT
#include "matte/PlaceholderMatte.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aibgr {

namespace {

void collect(const FloatImage& image, int x, int y, std::vector<float>& rs, std::vector<float>& gs,
             std::vector<float>& bs) {
    const float* p = image.pixel(x, y);
    rs.push_back(p[0]);
    gs.push_back(p[1]);
    bs.push_back(p[2]);
}

float median(std::vector<float>& v) noexcept {
    if (v.empty()) {
        return 0.0f;
    }
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + mid, v.end());
    return v[mid];
}

inline float smoothstep(float edge0, float edge1, float x) noexcept {
    if (edge1 <= edge0) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = std::min(1.0f, std::max(0.0f, (x - edge0) / (edge1 - edge0)));
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

BorderColour estimateBorderColour(const FloatImage& image, int border) noexcept {
    BorderColour out;
    if (image.empty()) {
        return out;
    }
    border = std::max(1, std::min(border, std::min(image.width(), image.height()) / 2));

    std::vector<float> rs, gs, bs;
    rs.reserve(static_cast<std::size_t>(border) * (image.width() + image.height()) * 2u);
    gs.reserve(rs.capacity());
    bs.reserve(rs.capacity());

    for (int b = 0; b < border; ++b) {
        for (int x = 0; x < image.width(); ++x) {
            collect(image, x, b, rs, gs, bs);
            collect(image, x, image.height() - 1 - b, rs, gs, bs);
        }
        for (int y = 0; y < image.height(); ++y) {
            collect(image, b, y, rs, gs, bs);
            collect(image, image.width() - 1 - b, y, rs, gs, bs);
        }
    }
    out.r = median(rs);
    out.g = median(gs);
    out.b = median(bs);
    out.samples = static_cast<int>(rs.size());
    return out;
}

void computeFallbackKeyMatte(const FloatImage& image, Matte& out,
                             const FallbackKeySettings& settings) noexcept {
    if (image.empty()) {
        return;
    }
    // The destination is sized here, not by the caller: the contract is "writes a matte the same
    // size as the image". Requiring the caller to pre-size it would silently no-op on a fresh Matte
    // and hand back an empty result that looks like success.
    if (out.width() != image.width() || out.height() != image.height()) {
        out.resize(image.width(), image.height());
    }

    const BorderColour bg = estimateBorderColour(image, settings.border);
    const float tolerance = std::min(1.0f, std::max(0.0f, settings.tolerance));
    const float softness = std::min(1.0f, std::max(0.0001f, settings.softness));
    // The key is on SQUARED distance (see colourDistanceSquared), so the tolerance band is
    // squared too. Comparing an unsquared tolerance against a squared distance would
    // shrink the effective key range for every tolerance below 1.0.
    const float edge0 = std::max(0.0f, (tolerance - softness) * (tolerance - softness));
    const float edge1 = (tolerance + softness) * (tolerance + softness);

    for (int y = 0; y < image.height(); ++y) {
        const float* s = image.row(y);
        float* d = out.row(y);
        for (int x = 0; x < image.width(); ++x, s += 4) {
            const float dist = colourDistanceSquared(s[0], s[1], s[2], bg.r, bg.g, bg.b);
            d[x] = smoothstep(edge0, edge1, dist);
        }
    }
}

} // namespace aibgr

// SPDX-License-Identifier: MIT
#include "core/FloatImage.h"

#include <algorithm>

namespace aibgr {

void FloatImage::resize(int width, int height) {
    if (width < 0) {
        width = 0;
    }
    if (height < 0) {
        height = 0;
    }
    width_ = width;
    height_ = height;
    const std::size_t needed =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * kChannels;
    if (needed > buffer_.size()) {
        // Grows capacity monotonically; shrink_to_fit is never called.
        buffer_.resize(needed);
    }
}

void FloatImage::fill(float r, float g, float b, float a) {
    if (empty()) {
        return;
    }
    for (int y = 0; y < height_; ++y) {
        float* p = row(y);
        for (int x = 0; x < width_; ++x) {
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = a;
            p += kChannels;
        }
    }
}

} // namespace aibgr

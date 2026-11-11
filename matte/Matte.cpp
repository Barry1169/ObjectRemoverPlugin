// SPDX-License-Identifier: MIT
#include "matte/Matte.h"

#include <algorithm>
#include <cmath>

namespace aibgr {

void Matte::resize(int width, int height) {
    if (width < 0) {
        width = 0;
    }
    if (height < 0) {
        height = 0;
    }
    width_ = width;
    height_ = height;
    const std::size_t needed = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (needed > buffer_.size()) {
        buffer_.resize(needed);
    }
}

void Matte::fill(float value) {
    const std::size_t n = floatCount();
    float* p = buffer_.data();
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = value;
    }
}

void Matte::clamp01() {
    const std::size_t n = floatCount();
    float* p = buffer_.data();
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = std::min(1.0f, std::max(0.0f, p[i]));
    }
}

double Matte::mean() const noexcept {
    const std::size_t n = floatCount();
    if (n == 0) {
        return 0.0;
    }
    double sum = 0.0;
    const float* p = buffer_.data();
    for (std::size_t i = 0; i < n; ++i) {
        sum += p[i];
    }
    return sum / static_cast<double>(n);
}

double Matte::ambiguousFraction(float lo, float hi) const noexcept {
    const std::size_t n = floatCount();
    if (n == 0) {
        return 0.0;
    }
    std::size_t count = 0;
    const float* p = buffer_.data();
    for (std::size_t i = 0; i < n; ++i) {
        if (p[i] > lo && p[i] < hi) {
            ++count;
        }
    }
    return static_cast<double>(count) / static_cast<double>(n);
}

} // namespace aibgr

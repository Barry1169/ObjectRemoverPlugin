// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <vector>

namespace aibgr {

/// Single-channel float32 alpha matte, values nominally in [0, 1].
///
/// Grow-only allocation, same policy as FloatImage: no heap traffic in the steady state.
///
/// A Matte may live at a different resolution from the frame it describes (that is the whole
/// point of running inference at 512 and refining at full resolution), so it carries its own
/// width/height and is never assumed to match the image.
class Matte {
public:
    Matte() = default;
    Matte(int width, int height) { resize(width, height); }

    void resize(int width, int height);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return width_ <= 0 || height_ <= 0; }

    std::size_t stride() const noexcept { return static_cast<std::size_t>(width_); }
    std::size_t floatCount() const noexcept { return stride() * static_cast<std::size_t>(height_); }
    std::size_t allocatedBytes() const noexcept { return buffer_.capacity() * sizeof(float); }

    float* data() noexcept { return buffer_.data(); }
    const float* data() const noexcept { return buffer_.data(); }

    float* row(int y) noexcept { return buffer_.data() + static_cast<std::size_t>(y) * stride(); }
    const float* row(int y) const noexcept {
        return buffer_.data() + static_cast<std::size_t>(y) * stride();
    }

    float& at(int x, int y) noexcept { return row(y)[x]; }
    float at(int x, int y) const noexcept { return row(y)[x]; }

    void fill(float value);
    void clear() { fill(0.0f); }

    /// Clamp every value into [0, 1]. Called after any operation that can overshoot.
    void clamp01();

    /// Mean value over the whole matte - cheap and useful for tests and diagnostics.
    double mean() const noexcept;

    /// Fraction of pixels strictly inside the ambiguous band (lo, hi). A hard binary mask gives
    /// ~0 here; a matte that preserves hair gives a meaningful value. This is the number the
    /// "detailed hair" test in docs/04 measures.
    double ambiguousFraction(float lo = 0.05f, float hi = 0.95f) const noexcept;

    void release() noexcept {
        std::vector<float>().swap(buffer_);
        width_ = 0;
        height_ = 0;
    }

private:
    std::vector<float> buffer_;
    int width_ = 0;
    int height_ = 0;
};

} // namespace aibgr

// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <vector>

namespace aibgr {

/// RGBA float32 working image.
///
/// Allocation policy is deliberately **grow-only**: `resize()` never shrinks the underlying
/// buffer, so after warm-up a steady render loop performs zero heap allocation. This is the
/// "avoid unnecessary memory allocations during rendering" requirement made concrete.
///
/// Contents after `resize()` are **undefined** unless the new size exceeded the previous
/// capacity (in which case only the newly appended tail is zero-filled). Callers that need a
/// known state must call `fill()` or write every pixel. The pipeline always writes every pixel
/// it hands on.
class FloatImage {
public:
    static constexpr int kChannels = 4;

    FloatImage() = default;
    FloatImage(int width, int height) { resize(width, height); }

    void resize(int width, int height);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    bool empty() const noexcept { return width_ <= 0 || height_ <= 0; }

    /// Floats per row (tightly packed, no padding).
    std::size_t stride() const noexcept { return static_cast<std::size_t>(width_) * kChannels; }
    std::size_t floatCount() const noexcept { return stride() * static_cast<std::size_t>(height_); }
    /// Bytes actually held, which may exceed floatCount() * 4 because of grow-only reuse.
    std::size_t allocatedBytes() const noexcept { return buffer_.capacity() * sizeof(float); }

    float* data() noexcept { return buffer_.data(); }
    const float* data() const noexcept { return buffer_.data(); }

    float* row(int y) noexcept { return buffer_.data() + static_cast<std::size_t>(y) * stride(); }
    const float* row(int y) const noexcept {
        return buffer_.data() + static_cast<std::size_t>(y) * stride();
    }

    float* pixel(int x, int y) noexcept { return row(y) + static_cast<std::size_t>(x) * kChannels; }
    const float* pixel(int x, int y) const noexcept {
        return row(y) + static_cast<std::size_t>(x) * kChannels;
    }

    void fill(float r, float g, float b, float a);
    void clearToTransparent() { fill(0.0f, 0.0f, 0.0f, 0.0f); }

    /// Releases the buffer. Only for the "clear cache / return memory" control - never call this
    /// between frames of a sequence.
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

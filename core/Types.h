// SPDX-License-Identifier: MIT
//
// Host-agnostic value types. Nothing under core/, matte/, composite/ or models/ may include an
// OFX header: that mapping lives in plugin/OfxImageAdapter. Keeping the boundary here is what
// lets the whole pipeline be unit-tested without a host, a GPU or Resolve.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>

namespace aibgr {

/// Mirrors the OFX bit depths (ofxCore.h:961-975) without depending on the header.
enum class BitDepth : uint8_t {
    None = 0,
    Byte,   ///< unsigned 8-bit integer, normalised 0..1
    Short,  ///< unsigned 16-bit integer, normalised 0..1
    Half,   ///< IEEE-754 binary16
    Float,  ///< IEEE-754 binary32
};

const char* bitDepthName(BitDepth d) noexcept;

/// Bytes per component, 0 for None.
int bitDepthComponentBytes(BitDepth d) noexcept;

/// Maximum integer value for the normalised integer depths (255 / 65535), 0 for float depths.
int bitDepthIntegerMax(BitDepth d) noexcept;

/// Mirrors the OFX component sets (ofxImageEffect.h:44-53).
enum class ComponentSet : uint8_t {
    None = 0,
    RGBA,
    RGB,
    Alpha,
};

const char* componentSetName(ComponentSet c) noexcept;

/// Components per pixel: RGBA=4, RGB=3, Alpha=1, None=0.
int componentCount(ComponentSet c) noexcept;

struct RectI {
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;

    constexpr int width() const noexcept { return x2 - x1; }
    constexpr int height() const noexcept { return y2 - y1; }
    constexpr bool empty() const noexcept { return x2 <= x1 || y2 <= y1; }
    constexpr std::size_t area() const noexcept {
        return empty() ? 0u : static_cast<std::size_t>(width()) * static_cast<std::size_t>(height());
    }

    constexpr RectI intersect(const RectI& o) const noexcept {
        return {std::max(x1, o.x1), std::max(y1, o.y1), std::min(x2, o.x2), std::min(y2, o.y2)};
    }

    /// Translate so that (x1, y1) becomes (0, 0). Used to index a buffer whose origin is the
    /// image bounds rather than the region of definition.
    constexpr RectI translatedToOrigin() const noexcept { return {0, 0, width(), height()}; }
};

inline RectI intersect(RectI a, RectI b) noexcept { return a.intersect(b); }

struct Size {
    int width = 0;
    int height = 0;
    constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
    constexpr std::size_t pixels() const noexcept {
        return empty() ? 0u : static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

/// Axis-aligned rectangle in canonical (double) coordinates, mirroring OfxRectD.
struct RectD {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    constexpr double width() const noexcept { return x2 - x1; }
    constexpr double height() const noexcept { return y2 - y1; }
    constexpr bool empty() const noexcept { return x2 <= x1 || y2 <= y1; }
};

/// Formats a rectangle for the log, e.g. "[0 0 1920 1080]".
///
/// Written with snprintf rather than a chain of operator+ for two reasons: this runs on the render
/// path inside log lines, so four heap-allocating std::to_string calls per frame are pure waste; and
/// `"[" + std::to_string(x)` selects operator+(const char*, std::string&&), which trips a known
/// GCC 12 -Wrestrict false positive (reported entirely from char_traits.h, with no user frame left
/// after inlining). Suppressing the warning globally would hide real overlapping-memcpy bugs, so the
/// concatenation is avoided instead.
inline std::string rectToString(const RectI& r) {
    char buf[64];
    const int n = std::snprintf(buf, sizeof(buf), "[%d %d %d %d]", r.x1, r.y1, r.x2, r.y2);
    if (n <= 0) {
        return std::string();
    }
    const int written = (n < static_cast<int>(sizeof(buf))) ? n : static_cast<int>(sizeof(buf)) - 1;
    return std::string(buf, static_cast<std::size_t>(written));
}

} // namespace aibgr

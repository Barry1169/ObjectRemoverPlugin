// SPDX-License-Identifier: MIT
#include <cstdint>
#include "core/PixelConvert.h"

#include <cstring>

namespace aibgr {

namespace {

inline float bitsToFloat(uint32_t bits) noexcept {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

inline uint32_t floatToBits(float f) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

/// Clamp to [0,1] then round-half-up onto an integer grid of `scale` steps.
inline uint32_t quantize(float f, float scale) noexcept {
    float v = f * scale;
    if (!(v > 0.0f)) { // also catches NaN
        return 0u;
    }
    if (v > scale) {
        v = scale;
    }
    return static_cast<uint32_t>(v + 0.5f);
}

} // namespace

float halfToFloat(uint16_t h) noexcept {
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    uint32_t exponent = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    uint32_t mantissa = static_cast<uint32_t>(h) & 0x3FFu;

    if (exponent == 0u) {
        if (mantissa == 0u) {
            return bitsToFloat(sign); // +/- zero
        }
        // Subnormal half: renormalise into a normal float.
        uint32_t e = 1u;
        while ((mantissa & 0x400u) == 0u) {
            mantissa <<= 1;
            --e;
        }
        mantissa &= ~0x400u;
        return bitsToFloat(sign | ((e + 127u - 15u) << 23) | (mantissa << 13));
    }

    if (exponent == 31u) {
        if (mantissa == 0u) {
            return bitsToFloat(sign | 0x7F800000u); // +/- infinity
        }
        // NaN: keep a non-zero payload so it does not silently become infinity.
        uint32_t payload = mantissa << 13;
        if (payload == 0u) {
            payload = 1u;
        }
        return bitsToFloat(sign | 0x7F800000u | payload);
    }

    return bitsToFloat(sign | ((exponent + 127u - 15u) << 23) | (mantissa << 13));
}

uint16_t floatToHalf(float f) noexcept {
    const uint32_t bits = floatToBits(f);
    // Already in half-bit position (float bit 31 -> half bit 15), so it is 0 or 0x8000 and always
    // fits the return type. Declaring it uint16_t keeps MSVC /W4 quiet about the bare `return
    // sign;` paths below without changing any value: C4244 was reporting the static type, not a
    // real truncation.
    const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
    const uint32_t biasedExponent = (bits >> 23) & 0xFFu;
    uint32_t mantissa = bits & 0x7FFFFFu;

    if (biasedExponent == 0xFFu) {
        if (mantissa == 0u) {
            return static_cast<uint16_t>(sign | 0x7C00u); // infinity
        }
        uint16_t payload = static_cast<uint16_t>(mantissa >> 13);
        if (payload == 0u) {
            payload = 1u; // never turn a NaN into an infinity
        }
        return static_cast<uint16_t>(sign | 0x7C00u | payload);
    }

    // Rebias: float exponent bias 127 -> half exponent bias 15.
    int32_t exponent = static_cast<int32_t>(biasedExponent) - 127 + 15;

    if (biasedExponent == 0u) {
        // Input was zero or a float subnormal; both underflow to half zero.
        return sign;
    }

    if (exponent >= 31) {
        return static_cast<uint16_t>(sign | 0x7C00u); // overflow -> infinity
    }

    if (exponent <= 0) {
        if (exponent < -10) {
            return sign; // underflow -> zero
        }
        // Half subnormal.
        mantissa |= 0x800000u; // restore the implicit leading 1
        const int shift = 14 - static_cast<int>(exponent);
        const uint32_t halfMantissa = mantissa >> shift;
        const uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const uint32_t halfway = 1u << (shift - 1);
        const uint32_t rounded =
            (remainder > halfway || (remainder == halfway && (halfMantissa & 1u) != 0u))
                ? halfMantissa + 1u
                : halfMantissa;
        return static_cast<uint16_t>(sign | rounded);
    }

    uint32_t halfMantissa = mantissa >> 13;
    const uint32_t remainder = mantissa & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (halfMantissa & 1u) != 0u)) {
        ++halfMantissa;
        if (halfMantissa == 0x400u) { // mantissa carried into the exponent
            halfMantissa = 0u;
            ++exponent;
            if (exponent >= 31) {
                return static_cast<uint16_t>(sign | 0x7C00u);
            }
        }
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | halfMantissa);
}

void componentsToFloat(const void* src, BitDepth depth, float* dst, int count) noexcept {
    if (count <= 0 || src == nullptr || dst == nullptr) {
        return;
    }
    switch (depth) {
    case BitDepth::Byte: {
        const auto* s = static_cast<const uint8_t*>(src);
        constexpr float kScale = 1.0f / 255.0f;
        for (int i = 0; i < count; ++i) {
            dst[i] = static_cast<float>(s[i]) * kScale;
        }
        break;
    }
    case BitDepth::Short: {
        const auto* s = static_cast<const uint16_t*>(src);
        constexpr float kScale = 1.0f / 65535.0f;
        for (int i = 0; i < count; ++i) {
            dst[i] = static_cast<float>(s[i]) * kScale;
        }
        break;
    }
    case BitDepth::Half: {
        const auto* s = static_cast<const uint16_t*>(src);
        for (int i = 0; i < count; ++i) {
            dst[i] = halfToFloat(s[i]);
        }
        break;
    }
    case BitDepth::Float: {
        std::memcpy(dst, src, static_cast<std::size_t>(count) * sizeof(float));
        break;
    }
    case BitDepth::None:
        break;
    }
}

void componentsFromFloat(const float* src, void* dst, BitDepth depth, int count) noexcept {
    if (count <= 0 || src == nullptr || dst == nullptr) {
        return;
    }
    switch (depth) {
    case BitDepth::Byte: {
        auto* d = static_cast<uint8_t*>(dst);
        for (int i = 0; i < count; ++i) {
            d[i] = static_cast<uint8_t>(quantize(src[i], 255.0f));
        }
        break;
    }
    case BitDepth::Short: {
        auto* d = static_cast<uint16_t*>(dst);
        for (int i = 0; i < count; ++i) {
            d[i] = static_cast<uint16_t>(quantize(src[i], 65535.0f));
        }
        break;
    }
    case BitDepth::Half: {
        auto* d = static_cast<uint16_t*>(dst);
        for (int i = 0; i < count; ++i) {
            d[i] = floatToHalf(src[i]);
        }
        break;
    }
    case BitDepth::Float: {
        std::memcpy(dst, src, static_cast<std::size_t>(count) * sizeof(float));
        break;
    }
    case BitDepth::None:
        break;
    }
}

void pixelToFloat(const void* src, BitDepth depth, float outRGBA[4]) noexcept {
    componentsToFloat(src, depth, outRGBA, 4);
}

void pixelFromFloat(const float inRGBA[4], void* dst, BitDepth depth) noexcept {
    componentsFromFloat(inRGBA, dst, depth, 4);
}

} // namespace aibgr

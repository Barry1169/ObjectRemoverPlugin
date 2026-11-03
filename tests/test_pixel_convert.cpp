// SPDX-License-Identifier: MIT
//
// Pixel format conversion. These are the tests that catch the two failure modes that are
// invisible in a screenshot until someone looks closely: integer depths treated as floats, and a
// broken half-float conversion that produces a subtly banded matte.

#include "TestCase.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "core/PixelConvert.h"
#include "core/Types.h"

using namespace aibgr;

namespace {

/// Round-trips a value through half and back. Half has ~11 bits of mantissa, so the tolerance is
/// relative, not absolute.
void checkHalfRoundTrip(float value, float relTolerance) {
    const uint16_t h = floatToHalf(value);
    const float back = halfToFloat(h);
    const float scale = std::max(1e-6f, std::fabs(value));
    CHECK_MSG(std::fabs(back - value) <= relTolerance * scale,
              "half round trip of " + std::to_string(value) + " gave " + std::to_string(back));
}

} // namespace

TEST(pixelConvert, byteDepthNormalisesToUnitRange) {
    // Byte is an UNSIGNED 8-BIT INTEGER (ofxCore.h:961-965), not a float. Normalising by 255 is
    // the whole point; treating it as a float would give an all-white image.
    uint8_t src[4] = {0, 128, 255, 64};
    float dst[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    componentsToFloat(src, BitDepth::Byte, dst, 4);

    CHECK_NEAR(dst[0], 0.0f, 1e-6);
    CHECK_NEAR(dst[2], 1.0f, 1e-6);
    CHECK_NEAR(dst[1], 128.0 / 255.0, 1e-6);
    CHECK_NEAR(dst[3], 64.0 / 255.0, 1e-6);
}

TEST(pixelConvert, shortDepthNormalisesBy65535) {
    // Short is an UNSIGNED 16-BIT INTEGER (ofxCore.h:966-968). This is the single most commonly
    // gotten wrong depth, because "Short" sounds like a half-float.
    uint16_t src[3] = {0, 32768, 65535};
    float dst[3] = {-1.0f, -1.0f, -1.0f};
    componentsToFloat(src, BitDepth::Short, dst, 3);

    CHECK_NEAR(dst[0], 0.0f, 1e-6);
    CHECK_NEAR(dst[2], 1.0f, 1e-6);
    CHECK_NEAR(dst[1], 32768.0 / 65535.0, 1e-5);
}

TEST(pixelConvert, byteAndShortRoundTrip) {
    for (int v = 0; v <= 255; ++v) {
        const uint8_t byteIn = static_cast<uint8_t>(v);
        float f = 0.0f;
        componentsToFloat(&byteIn, BitDepth::Byte, &f, 1);
        uint8_t byteOut = 0;
        componentsFromFloat(&f, &byteOut, BitDepth::Byte, 1);
        CHECK_MSG(byteOut == byteIn, "Byte round trip failed at " + std::to_string(v));
    }

    // Spot-check Short across the range rather than all 65536 values.
    const uint16_t samples[] = {0, 1, 255, 256, 32767, 32768, 65534, 65535};
    for (uint16_t v : samples) {
        float f = 0.0f;
        componentsToFloat(&v, BitDepth::Short, &f, 1);
        uint16_t out = 0;
        componentsFromFloat(&f, &out, BitDepth::Short, 1);
        CHECK_MSG(out == v, "Short round trip failed at " + std::to_string(v));
    }
}

TEST(pixelConvert, floatDepthIsCopiedVerbatim) {
    // A compositor must be able to carry values above 1.0 (highlights). Clamping float input would
    // destroy HDR data and is exactly the kind of "no colour shift" violation section 9 asks about.
    const float src[4] = {-0.25f, 0.5f, 1.0f, 4.7f};
    float dst[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    componentsToFloat(src, BitDepth::Float, dst, 4);
    for (int i = 0; i < 4; ++i) {
        CHECK_EXACT(dst[i], src[i]);
    }

    float back[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    componentsFromFloat(dst, back, BitDepth::Float, 4);
    for (int i = 0; i < 4; ++i) {
        CHECK_EXACT(back[i], src[i]);
    }
}

TEST(pixelConvert, halfSpecialValues) {
    CHECK_NEAR(halfToFloat(0x0000), 0.0f, 0.0);
    CHECK_NEAR(halfToFloat(0x8000), -0.0f, 0.0);
    CHECK_NEAR(halfToFloat(0x3C00), 1.0f, 1e-6);
    CHECK_NEAR(halfToFloat(0xBC00), -1.0f, 1e-6);
    CHECK_NEAR(halfToFloat(0x3800), 0.5f, 1e-6);
    CHECK_NEAR(halfToFloat(0x4200), 3.0f, 1e-6);

    CHECK(std::isinf(halfToFloat(0x7C00)));
    CHECK(halfToFloat(0x7C00) > 0.0f);
    CHECK(std::isinf(halfToFloat(0xFC00)));
    CHECK(halfToFloat(0xFC00) < 0.0f);
    CHECK(std::isnan(halfToFloat(0x7E00)));

    // Smallest normal and a denormal.
    CHECK_NEAR(halfToFloat(0x0400), 6.103515625e-5f, 1e-12);
    CHECK_NEAR(halfToFloat(0x0001), 5.9604644775390625e-8f, 1e-15);
}

TEST(pixelConvert, halfRoundTrips) {
    checkHalfRoundTrip(0.0f, 0.0f);
    checkHalfRoundTrip(1.0f, 1e-3f);
    checkHalfRoundTrip(-1.0f, 1e-3f);
    checkHalfRoundTrip(0.18f, 1e-3f); // mid grey, the value that matters most for matting
    checkHalfRoundTrip(0.001f, 1e-2f);
    checkHalfRoundTrip(65504.0f, 1e-3f); // largest finite half

    // Overflow saturates to +/-Inf rather than wrapping to a negative number.
    CHECK(std::isinf(halfToFloat(floatToHalf(70000.0f))));
    CHECK(halfToFloat(floatToHalf(70000.0f)) > 0.0f);
    CHECK(std::isinf(halfToFloat(floatToHalf(-70000.0f))));
    CHECK(halfToFloat(floatToHalf(-70000.0f)) < 0.0f);

    // NaN survives.
    CHECK(std::isnan(halfToFloat(floatToHalf(std::numeric_limits<float>::quiet_NaN()))));
}

TEST(pixelConvert, pixelHelpers) {
    uint8_t rgba[4] = {255, 0, 128, 255};
    float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    pixelToFloat(rgba, BitDepth::Byte, out);
    CHECK_NEAR(out[0], 1.0f, 1e-6);
    CHECK_NEAR(out[1], 0.0f, 1e-6);
    CHECK_NEAR(out[2], 128.0 / 255.0, 1e-6);
    CHECK_NEAR(out[3], 1.0f, 1e-6);

    uint8_t back[4] = {0, 0, 0, 0};
    pixelFromFloat(out, back, BitDepth::Byte);
    for (int i = 0; i < 4; ++i) {
        CHECK_MSG(back[i] == rgba[i], "pixel round trip failed on channel " + std::to_string(i));
    }
}

TEST(pixelConvert, integerConversionClampsAndRounds) {
    // Out-of-range floats must clamp, not wrap: 1.5 written to a Byte has to become 255, not 127.
    const float high[4] = {1.5f, 2.0f, 100.0f, 1.0f};
    uint8_t byteOut[4] = {0, 0, 0, 0};
    componentsFromFloat(high, byteOut, BitDepth::Byte, 4);
    CHECK_EQ(static_cast<int>(byteOut[0]), 255);
    CHECK_EQ(static_cast<int>(byteOut[1]), 255);
    CHECK_EQ(static_cast<int>(byteOut[2]), 255);

    const float low[4] = {-0.5f, -100.0f, 0.0f, 0.0f};
    componentsFromFloat(low, byteOut, BitDepth::Byte, 4);
    CHECK_EQ(static_cast<int>(byteOut[0]), 0);
    CHECK_EQ(static_cast<int>(byteOut[1]), 0);

    // Rounds to nearest rather than truncating: 0.5 * 255 = 127.5 -> 128, not 127.
    const float mid[1] = {0.5f};
    componentsFromFloat(mid, byteOut, BitDepth::Byte, 1);
    CHECK_EQ(static_cast<int>(byteOut[0]), 128);

    // NaN must not become a huge value through an undefined cast.
    const float nan[1] = {std::numeric_limits<float>::quiet_NaN()};
    componentsFromFloat(nan, byteOut, BitDepth::Byte, 1);
    CHECK_EQ(static_cast<int>(byteOut[0]), 0);
}

TEST(pixelConvert, bitDepthMetadata) {
    CHECK_EQ(bitDepthComponentBytes(BitDepth::Byte), 1);
    CHECK_EQ(bitDepthComponentBytes(BitDepth::Short), 2);
    CHECK_EQ(bitDepthComponentBytes(BitDepth::Half), 2);
    CHECK_EQ(bitDepthComponentBytes(BitDepth::Float), 4);
    CHECK_EQ(bitDepthComponentBytes(BitDepth::None), 0);

    CHECK_EQ(bitDepthIntegerMax(BitDepth::Byte), 255);
    CHECK_EQ(bitDepthIntegerMax(BitDepth::Short), 65535);
    // Float depths have no integer maximum; returning 0 keeps callers from dividing by it.
    CHECK_EQ(bitDepthIntegerMax(BitDepth::Half), 0);
    CHECK_EQ(bitDepthIntegerMax(BitDepth::Float), 0);
}

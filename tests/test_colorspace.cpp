// SPDX-License-Identifier: MIT
//
// Colour space handling. The acceptance criterion these tests protect is "no colour shifts": the
// transform must be applied ONLY to the buffer handed to the segmentation network, and the
// Display-Referred path (which is what Resolve sends by default) must be an exact no-op.

#include "TestCase.h"

#include <cmath>
#include <cstring>

#include "core/ColorSpace.h"
#include "core/FloatImage.h"

using namespace aibgr;

TEST(colorSpace, srgbEndpointsAreExact) {
    CHECK_NEAR(srgbEncode(0.0f), 0.0f, 1e-6);
    CHECK_NEAR(srgbEncode(1.0f), 1.0f, 1e-5);
    CHECK_NEAR(srgbDecode(0.0f), 0.0f, 1e-6);
    CHECK_NEAR(srgbDecode(1.0f), 1.0f, 1e-5);
}

TEST(colorSpace, srgbRoundTrips) {
    const float values[] = {0.0f, 1e-5f, 0.0031308f, 0.01f, 0.18f, 0.5f, 0.9f, 1.0f};
    for (float v : values) {
        const float back = srgbDecode(srgbEncode(v));
        CHECK_MSG(std::fabs(back - v) < 1e-5f,
                  "sRGB round trip of " + std::to_string(v) + " gave " + std::to_string(back));
    }
}

TEST(colorSpace, srgbIsMonotonic) {
    float previous = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float linear = static_cast<float>(i) / 1000.0f;
        const float encoded = srgbEncode(linear);
        CHECK_MSG(encoded >= previous, "srgbEncode is not monotonic at " + std::to_string(linear));
        previous = encoded;
    }
}

TEST(colorSpace, srgbCollapsesInvalidInput) {
    // Negatives and NaN must not survive into a network input tensor.
    CHECK_NEAR(srgbEncode(-0.5f), 0.0f, 0.0);
    CHECK_NEAR(srgbEncode(std::nanf("")), 0.0f, 0.0);
    CHECK_NEAR(srgbDecode(-0.5f), 0.0f, 0.0);
    CHECK_NEAR(srgbDecode(std::nanf("")), 0.0f, 0.0);
}

TEST(colorSpace, srgbEncodeIsAboveLinearForMidtones) {
    // The defining shape of a display encoding: 0.18 linear becomes roughly 0.46 encoded.
    const float encoded = srgbEncode(0.18f);
    CHECK(encoded > 0.18f);
    CHECK_NEAR(encoded, 0.4623f, 5e-3);
}

TEST(colorSpace, logCMatchesPublishedAnchors) {
    // ARRI LogC EI 800. These two anchors are the standard sanity check: encoding 1.0 (white) gives
    // about 0.5583 and encoding 0.18 (mid grey) gives about 0.3643. So DECODING those encoded
    // values must return the original scene-linear numbers.
    CHECK_NEAR(logCToLinear(0.5583f), 1.0f, 5e-3);
    CHECK_NEAR(logCToLinear(0.3643f), 0.18f, 5e-3);
}

TEST(colorSpace, logCIsMonotonicAndBounded) {
    float previous = -1.0f;
    for (int i = 0; i <= 1000; ++i) {
        const float encoded = static_cast<float>(i) / 1000.0f;
        const float linear = logCToLinear(encoded);
        CHECK_MSG(linear >= previous - 1e-6f,
                  "logCToLinear is not monotonic at " + std::to_string(encoded));
        CHECK_MSG(std::isfinite(linear), "logCToLinear produced a non-finite value");
        previous = linear;
    }
    // Black stays black, and nothing decodes to a negative light value.
    CHECK_NEAR(logCToLinear(0.0f), 0.0f, 1e-3);
}

TEST(colorSpace, displayReferredIsAnExactNoOp) {
    // The most important test in this file. Resolve hands us display-referred pixels by default; if
    // this path touched a single bit, every render would carry a colour shift.
    FloatImage img(3, 2);
    const float values[3 * 2 * 4] = {
        0.0f,   0.18f, 0.4623f, 1.0f, 0.9f, 0.1f,  0.05f, 0.5f,
        0.25f,  0.75f, 1.0f,    0.0f, 0.3f, 0.33f, 0.66f, 0.001f,
        -0.02f, 1.5f,  2.0f,    0.7f, 0.0f, 0.0f,  0.0f,  0.0f,
    };
    std::memcpy(img.data(), values, sizeof(values));

    toInferenceEncoding(img, ColorSpaceAssumption::DisplayReferred);

    for (size_t i = 0; i < sizeof(values) / sizeof(float); ++i) {
        CHECK_MSG(img.data()[i] == values[i],
                  "Display-Referred encoding changed pixel data at index " + std::to_string(i));
    }
}

TEST(colorSpace, encodingLeavesAlphaUntouched) {
    // Alpha is coverage, not light. Encoding it would corrupt the matte the network is meant to
    // predict against.
    FloatImage img(2, 1);
    img.fill(0.2f, 0.4f, 0.6f, 0.37f);

    toInferenceEncoding(img, ColorSpaceAssumption::SceneLinear);

    CHECK_EXACT(img.pixel(0, 0)[3], 0.37f);
    CHECK_EXACT(img.pixel(1, 0)[3], 0.37f);
    // ...and the colour really did change.
    CHECK(img.pixel(0, 0)[0] != 0.2f);
}

TEST(colorSpace, sceneLinearIsEncodedUpwards) {
    FloatImage img(1, 1);
    img.fill(0.18f, 0.18f, 0.18f, 1.0f);
    toInferenceEncoding(img, ColorSpaceAssumption::SceneLinear);
    CHECK_NEAR(img.pixel(0, 0)[0], srgbEncode(0.18f), 1e-5);
}

TEST(colorSpace, logCDecodesThenEncodes) {
    // LogC is a two-step path: decode to scene linear, then encode to sRGB. Doing only the second
    // half is the classic mistake and produces a washed-out matte.
    FloatImage img(1, 1);
    img.fill(0.3643f, 0.3643f, 0.3643f, 1.0f);
    toInferenceEncoding(img, ColorSpaceAssumption::LogC);
    CHECK_NEAR(img.pixel(0, 0)[0], srgbEncode(logCToLinear(0.3643f)), 1e-5);
    // ...which lands near the sRGB encoding of mid grey.
    CHECK_NEAR(img.pixel(0, 0)[0], srgbEncode(0.18f), 5e-3);
}

TEST(colorSpace, autoDetectSeparatesLinearFromDisplay) {
    // A dark image is almost certainly scene-linear: a display-referred picture with mean luminance
    // below 0.25 would be very dark indeed, whereas 0.18 mid grey in linear light is normal.
    FloatImage dark(8, 8);
    dark.fill(0.18f, 0.18f, 0.18f, 1.0f);
    CHECK(autoDetect(dark) == ColorSpaceAssumption::SceneLinear);

    FloatImage bright(8, 8);
    bright.fill(0.7f, 0.7f, 0.7f, 1.0f);
    CHECK(autoDetect(bright) == ColorSpaceAssumption::DisplayReferred);

    // Auto through toInferenceEncoding must behave the same as the explicit choice.
    FloatImage a(2, 2);
    a.fill(0.18f, 0.18f, 0.18f, 1.0f);
    FloatImage b(2, 2);
    b.fill(0.18f, 0.18f, 0.18f, 1.0f);
    toInferenceEncoding(a, ColorSpaceAssumption::Auto);
    toInferenceEncoding(b, ColorSpaceAssumption::SceneLinear);
    CHECK_EXACT(a.pixel(0, 0)[0], b.pixel(0, 0)[0]);
}

TEST(colorSpace, emptyImageIsSafe) {
    FloatImage empty;
    toInferenceEncoding(empty, ColorSpaceAssumption::LogC); // must not crash
    CHECK(empty.empty());
    CHECK(autoDetect(empty) == ColorSpaceAssumption::SceneLinear);
}

TEST(colorSpace, namesAreDistinct) {
    const char* names[] = {
        colorSpaceName(ColorSpaceAssumption::Auto),
        colorSpaceName(ColorSpaceAssumption::DisplayReferred),
        colorSpaceName(ColorSpaceAssumption::SceneLinear),
        colorSpaceName(ColorSpaceAssumption::LogC),
    };
    for (int i = 0; i < 4; ++i) {
        CHECK(names[i] != nullptr);
        for (int j = i + 1; j < 4; ++j) {
            CHECK_MSG(std::strcmp(names[i], names[j]) != 0,
                      std::string("duplicate colour space name: ") + names[i]);
        }
    }
}

// SPDX-License-Identifier: MIT
//
// The built-in colour key. This is what the plugin falls back to when no AI model is available, so
// "correct behaviour when models are unavailable" from section 9 of the brief is tested here rather
// than assumed.

#include "TestCase.h"

#include <cmath>

#include "core/FloatImage.h"
#include "matte/Matte.h"
#include "matte/PlaceholderMatte.h"

using namespace aibgr;

namespace {

/// A red rectangle on a green field. The border is entirely green, so the key's background estimate
/// is green and the rectangle is the only foreground.
FloatImage makeSubjectFrame(int w, int h, int x0, int y0, int x1, int y1) {
    FloatImage img(w, h);
    img.fill(0.1f, 0.7f, 0.2f, 1.0f); // green background
    for (int y = y0; y < y1 && y < h; ++y) {
        for (int x = x0; x < x1 && x < w; ++x) {
            float* p = img.pixel(x, y);
            p[0] = 0.9f;
            p[1] = 0.1f;
            p[2] = 0.1f;
            p[3] = 1.0f;
        }
    }
    return img;
}

} // namespace

TEST(placeholderMatte, colourDistanceSquaredIsNormalisedAndSymmetric) {
    CHECK_NEAR(colourDistanceSquared(0, 0, 0, 0, 0, 0), 0.0f, 0.0);
    CHECK_NEAR(colourDistanceSquared(0, 0, 0, 1, 1, 1), 1.0f, 1e-6); // black vs white
    CHECK_NEAR(colourDistanceSquared(1, 0, 0, 0, 0, 0), 1.0f / 3.0f, 1e-6); // one channel differs
    CHECK_NEAR(colourDistanceSquared(0.2f, 0.4f, 0.6f, 0.7f, 0.1f, 0.9f),
               colourDistanceSquared(0.7f, 0.1f, 0.9f, 0.2f, 0.4f, 0.6f), 1e-6);
}

TEST(placeholderMatte, borderEstimateUsesTheMedianNotTheMean) {
    // A subject touching one edge must not drag the estimate. With a median, three green edges and
    // one red edge still give green; with a mean the result would be muddy.
    FloatImage img(16, 16);
    img.fill(0.1f, 0.7f, 0.2f, 1.0f);
    for (int y = 0; y < 16; ++y) { // red left edge, subject touching the frame
        float* p = img.pixel(0, y);
        p[0] = 0.95f;
        p[1] = 0.05f;
        p[2] = 0.05f;
    }

    const BorderColour b = estimateBorderColour(img, 4);
    CHECK(b.samples > 0);
    CHECK_MSG(b.g > b.r, "median border estimate was dragged towards the subject");
    CHECK_NEAR(b.g, 0.7f, 0.05f);
}

TEST(placeholderMatte, uniformFrameGivesUniformMatte) {
    FloatImage img(12, 12);
    img.fill(0.4f, 0.5f, 0.6f, 1.0f);
    Matte out;
    computeFallbackKeyMatte(img, out, FallbackKeySettings{});

    CHECK_EQ(out.width(), 12);
    CHECK_EQ(out.height(), 12);
    // Everything matches the background estimate, so everything is background.
    CHECK_NEAR(out.mean(), 0.0, 1e-5);
}

TEST(placeholderMatte, separatesASubjectFromAFlatBackground) {
    FloatImage img = makeSubjectFrame(32, 32, 10, 10, 22, 22);
    Matte out;
    FallbackKeySettings settings;
    settings.tolerance = 0.25f;
    settings.softness = 0.08f;
    computeFallbackKeyMatte(img, out, settings);

    // Centre of the subject is foreground, a corner of the background is not.
    CHECK_MSG(out.at(16, 16) > 0.9f, "subject centre was not detected as foreground");
    CHECK_MSG(out.at(2, 2) < 0.1f, "background corner was not detected as background");

    // And the matte actually separates: most pixels are near 0 or 1, not mushily in between.
    CHECK(out.ambiguousFraction() < 0.5);
}

TEST(placeholderMatte, matteIsAlwaysInRange) {
    FloatImage img = makeSubjectFrame(24, 24, 4, 4, 20, 20);
    for (float tolerance : {0.0f, 0.05f, 0.25f, 0.5f, 1.0f}) {
        Matte out;
        FallbackKeySettings settings;
        settings.tolerance = tolerance;
        computeFallbackKeyMatte(img, out, settings);
        for (int y = 0; y < out.height(); ++y) {
            for (int x = 0; x < out.width(); ++x) {
                const float v = out.at(x, y);
                CHECK_MSG(v >= 0.0f && v <= 1.0f,
                          "matte out of range at tolerance " + std::to_string(tolerance));
            }
        }
    }
}

TEST(placeholderMatte, toleranceExtremesBehaveLikeAKeyer) {
    // tolerance is the distance at which a pixel becomes 50% foreground, so it behaves like a
    // keyer's tolerance: LOW keeps more foreground, HIGH keys more away.
    FloatImage img = makeSubjectFrame(16, 16, 4, 4, 12, 12);

    // tolerance 1.0 puts the band at the maximum possible colour distance (black vs white), so
    // nothing in an ordinary frame is far enough from the background to count as foreground.
    Matte high;
    FallbackKeySettings s1;
    s1.tolerance = 1.0f;
    s1.softness = 0.0f;
    computeFallbackKeyMatte(img, high, s1);
    CHECK_NEAR(high.mean(), 0.0, 1e-5);

    // tolerance 0.0 means only an exact colour match reads as background; the subject, and any
    // difference at all, reads as foreground.
    Matte low;
    FallbackKeySettings s0;
    s0.tolerance = 0.0f;
    s0.softness = 0.0f;
    computeFallbackKeyMatte(img, low, s0);
    CHECK_NEAR(low.at(2, 2), 0.0f, 1e-6);  // background: matches the border estimate exactly
    CHECK_NEAR(low.at(8, 8), 1.0f, 1e-6);  // subject: far from the border estimate
    CHECK(low.mean() > high.mean());

    // Raising the tolerance from the default must key MORE away, never less - that is the direction
    // the inspector hint tells the artist to move it in.
    Matte mid;
    FallbackKeySettings s2;
    s2.tolerance = 0.25f;
    s2.softness = 0.08f;
    computeFallbackKeyMatte(img, mid, s2);
    Matte wide;
    FallbackKeySettings s3;
    s3.tolerance = 0.6f;
    s3.softness = 0.08f;
    computeFallbackKeyMatte(img, wide, s3);
    CHECK_MSG(wide.mean() <= mid.mean() + 1e-6,
              "a wider tolerance keyed away less of the frame");
}

TEST(placeholderMatte, softnessProducesATransition) {
    // Softness is what keeps the fallback from producing a hard, aliased silhouette. With a wide
    // soft band there must be a meaningful number of intermediate values.
    FloatImage img(32, 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float t = static_cast<float>(x) / 31.0f; // horizontal gradient
            float* p = img.pixel(x, y);
            p[0] = 0.1f + 0.8f * t;
            p[1] = 0.7f - 0.6f * t;
            p[2] = 0.2f;
            p[3] = 1.0f;
        }
    }

    Matte soft;
    FallbackKeySettings settings;
    settings.tolerance = 0.3f;
    settings.softness = 0.3f;
    computeFallbackKeyMatte(img, soft, settings);

    CHECK_MSG(soft.ambiguousFraction() > 0.05,
              "a wide softness band produced no intermediate matte values");
}

TEST(placeholderMatte, emptyImageIsSafe) {
    FloatImage img;
    Matte out;
    computeFallbackKeyMatte(img, out, FallbackKeySettings{}); // must not crash
    CHECK(out.empty());

    const BorderColour b = estimateBorderColour(img, 4);
    CHECK_EQ(b.samples, 0);
}

TEST(placeholderMatte, borderWidthIsClampedToTheImage) {
    // A 4-pixel border on a 3x3 image must not read outside it.
    FloatImage img(3, 3);
    img.fill(0.5f, 0.5f, 0.5f, 1.0f);
    const BorderColour b = estimateBorderColour(img, 4);
    CHECK(b.samples > 0);
    CHECK(std::isfinite(b.r));

    Matte out;
    computeFallbackKeyMatte(img, out, FallbackKeySettings{});
    CHECK_EQ(out.width(), 3);
    CHECK_EQ(out.height(), 3);
}

TEST(placeholderMatte, transparentFrameDoesNotProduceNan) {
    FloatImage img(8, 8);
    img.clearToTransparent(); // alpha 0 everywhere
    Matte out;
    computeFallbackKeyMatte(img, out, FallbackKeySettings{});
    for (int y = 0; y < out.height(); ++y) {
        for (int x = 0; x < out.width(); ++x) {
            CHECK_MSG(std::isfinite(out.at(x, y)), "transparent frame produced a non-finite matte");
        }
    }
}

TEST(placeholderMatte, softnessWiderThanToleranceKeepsExactBackgroundTransparent) {
    FloatImage image = makeSubjectFrame(16, 16, 4, 4, 12, 12);
    FallbackKeySettings settings;
    settings.tolerance = 0.1f;
    settings.softness = 0.5f;
    Matte matte;
    computeFallbackKeyMatte(image, matte, settings);
    CHECK_NEAR(matte.at(2, 2), 0.0f, 1e-6);
    CHECK(matte.at(8, 8) > 0.0f);
}

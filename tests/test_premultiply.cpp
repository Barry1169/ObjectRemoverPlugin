// SPDX-License-Identifier: MIT
//
// Premultiplication. This is where "unintended black backgrounds" and "bright halo around the
// subject" both come from, so the zero-alpha case gets its own test rather than being folded into a
// round-trip check.

#include "TestCase.h"

#include <cmath>
#include <limits>

#include "core/FloatImage.h"
#include "core/Premultiply.h"

using namespace aibgr;

TEST(premultiply, multipliesRgbByAlpha) {
    float rgba[8] = {
        1.0f, 0.5f, 0.25f, 1.0f, // opaque pixel: unchanged
        0.8f, 0.4f, 0.2f, 0.5f,  // half transparent
    };
    premultiplyRow(rgba, 2);

    CHECK_EXACT(rgba[0], 1.0f);
    CHECK_EXACT(rgba[1], 0.5f);
    CHECK_EXACT(rgba[2], 0.25f);
    CHECK_EXACT(rgba[3], 1.0f);

    CHECK_NEAR(rgba[4], 0.4f, 1e-6);
    CHECK_NEAR(rgba[5], 0.2f, 1e-6);
    CHECK_NEAR(rgba[6], 0.1f, 1e-6);
    CHECK_EXACT(rgba[7], 0.5f);
}

TEST(premultiply, zeroAlphaRecoversAsBlackNotGarbage) {
    // THE halo test. Recovering straight colour from premultiplied means dividing by alpha. At
    // alpha == 0 the original colour is genuinely gone, and dividing anyway produces inf or a huge
    // value that shows up as a bright fringe once the matte is feathered. The implementation must
    // return 0 instead.
    float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    unpremultiplyRow(rgba, 1);

    for (int c = 0; c < 3; ++c) {
        CHECK_MSG(std::isfinite(rgba[c]), "unpremultiply at alpha 0 produced a non-finite colour");
        CHECK_NEAR(rgba[c], 0.0f, 0.0);
    }
    CHECK_NEAR(rgba[3], 0.0f, 0.0);
}

TEST(premultiply, subEpsilonAlphaRecoversAsBlack) {
    // Just below the epsilon the division would give 1e-4 / 1e-5 = 10, which is nonsense for a
    // colour. It must be zeroed by the same rule.
    float rgba[4] = {1e-5f, 2e-5f, 3e-5f, 1e-5f};
    unpremultiplyRow(rgba, 1);

    CHECK_NEAR(rgba[0], 0.0f, 0.0);
    CHECK_NEAR(rgba[1], 0.0f, 0.0);
    CHECK_NEAR(rgba[2], 0.0f, 0.0);
}

TEST(premultiply, roundTripsForUsableAlpha) {
    const float alphas[] = {0.001f, 0.01f, 0.25f, 0.5f, 0.75f, 0.999f, 1.0f};
    for (float a : alphas) {
        const float original[4] = {0.9f, 0.4f, 0.15f, a};
        float rgba[4] = {original[0], original[1], original[2], original[3]};

        premultiplyRow(rgba, 1);
        // Premultiplied colour can never exceed its own alpha; that is the invariant the heuristic
        // below relies on.
        for (int c = 0; c < 3; ++c) {
            CHECK_MSG(rgba[c] <= rgba[3] + 1e-6f,
                      "premultiplied colour exceeded alpha at a=" + std::to_string(a));
        }

        unpremultiplyRow(rgba, 1);
        for (int c = 0; c < 3; ++c) {
            CHECK_MSG(std::fabs(rgba[c] - original[c]) < 1e-4f,
                      "round trip drifted at a=" + std::to_string(a));
        }
    }
}

TEST(premultiply, alphaChannelIsNeverModified) {
    float rgba[4] = {0.5f, 0.5f, 0.5f, 0.25f};
    premultiplyRow(rgba, 1);
    CHECK_EXACT(rgba[3], 0.25f);
    unpremultiplyRow(rgba, 1);
    CHECK_EXACT(rgba[3], 0.25f);
}

TEST(premultiply, wholeImageHelpers) {
    FloatImage img(2, 2);
    img.fill(0.8f, 0.6f, 0.4f, 0.5f);

    premultiply(img);
    CHECK_NEAR(img.pixel(0, 0)[0], 0.4f, 1e-6);
    CHECK_NEAR(img.pixel(1, 1)[3], 0.5f, 1e-6);

    unpremultiply(img);
    CHECK_NEAR(img.pixel(0, 0)[0], 0.8f, 1e-5);
    CHECK_NEAR(img.pixel(1, 1)[2], 0.4f, 1e-5);
}

TEST(premultiply, looksPremultipliedHeuristic) {
    // A straight image with colours above its alpha is definitely not premultiplied.
    FloatImage straight(4, 4);
    straight.fill(0.9f, 0.9f, 0.9f, 0.2f);
    CHECK(!looksPremultiplied(straight));

    // The premultiplied version of the same picture satisfies the invariant.
    FloatImage premult(4, 4);
    premult.fill(0.9f, 0.9f, 0.9f, 0.2f);
    premultiply(premult);
    CHECK(looksPremultiplied(premult));

    // An opaque image satisfies it trivially, which is why this stays a heuristic and not a proof.
    // The inspector exposes an explicit Follow Host / Premultiplied / Straight control for exactly
    // this reason.
    FloatImage opaque(4, 4);
    opaque.fill(0.5f, 0.5f, 0.5f, 1.0f);
    CHECK(looksPremultiplied(opaque));
}

TEST(premultiply, epsilonIsDocumentedValue) {
    CHECK_NEAR(kAlphaEpsilon, 1e-4f, 0.0);
}

TEST(premultiply, nanAlphaDoesNotPropagate) {
    // A malformed host buffer should degrade, not paint NaN across the frame.
    float rgba[4] = {0.5f, 0.5f, 0.5f, std::numeric_limits<float>::quiet_NaN()};
    unpremultiplyRow(rgba, 1);
    for (int c = 0; c < 3; ++c) {
        CHECK_MSG(!std::isnan(rgba[c]), "NaN alpha propagated into colour");
    }
}

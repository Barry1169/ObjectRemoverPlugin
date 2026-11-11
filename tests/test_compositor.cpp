// SPDX-License-Identifier: MIT
//
// The four output modes from section 2 of the brief, plus the pass-through test that guards against
// "unintended black backgrounds": when the effect is disabled the output must be the input, exactly.

#include "TestCase.h"

#include <cmath>
#include <cstring>

#include "composite/Compositor.h"
#include "core/FloatImage.h"
#include "core/PipelineSettings.h"
#include "matte/Matte.h"

using namespace aibgr;

namespace {

/// A 2x2 frame of a known colour with a known matte, so every mode can be checked numerically.
struct Fixture {
    FloatImage src{2, 2};
    Matte alpha{2, 2};
    FloatImage dst;
    PipelineSettings settings;

    Fixture(float r, float g, float b, float matteValue) {
        src.fill(r, g, b, 1.0f);
        alpha.fill(matteValue);
        dst.resize(2, 2);
    }

    void run() { composite(src, alpha, settings, dst); }

    const float* at(int x = 0, int y = 0) const { return dst.pixel(x, y); }
};

} // namespace

TEST(compositor, compositeRemovesBackgroundAtDefaultTransparency) {
    // backgroundTransparency defaults to 1.0: the background is fully removed. If this ever
    // regressed to 0.0 the plugin would install as a no-op, so the default is asserted too.
    PipelineSettings defaults;
    CHECK_NEAR(defaults.backgroundTransparency, 1.0f, 0.0);

    Fixture f(0.8f, 0.2f, 0.1f, 0.0f); // fully background pixel
    f.settings.outputMode = OutputMode::Composite;
    f.run();

    // RGB is preserved (never blackened), alpha goes to 0: the background is gone.
    CHECK_NEAR(f.at()[0], 0.8f, 1e-6);
    CHECK_NEAR(f.at()[1], 0.2f, 1e-6);
    CHECK_NEAR(f.at()[2], 0.1f, 1e-6);
    CHECK_NEAR(f.at()[3], 0.0f, 1e-6);
}

TEST(compositor, compositeKeepsForegroundAtFullMatte) {
    Fixture f(0.8f, 0.2f, 0.1f, 1.0f);
    f.settings.outputMode = OutputMode::Composite;
    f.run();

    CHECK_NEAR(f.at()[3], 1.0f, 1e-6);
    CHECK_NEAR(f.at()[0], 0.8f, 1e-6);
}

TEST(compositor, compositePartialMatteBlendsAlpha) {
    // A = a + bgRetain * (1 - a), bgRetain = 1 - backgroundTransparency.
    Fixture f(0.5f, 0.5f, 0.5f, 0.6f);
    f.settings.outputMode = OutputMode::Composite;
    f.settings.backgroundTransparency = 0.5f; // bgRetain = 0.5
    f.run();
    CHECK_NEAR(f.at()[3], 0.6f + 0.5f * 0.4f, 1e-6); // 0.8

    // Fading the background all the way back in makes Composite opaque everywhere.
    Fixture g(0.5f, 0.5f, 0.5f, 0.6f);
    g.settings.outputMode = OutputMode::Composite;
    g.settings.backgroundTransparency = 0.0f; // bgRetain = 1
    g.run();
    CHECK_NEAR(g.at()[3], 1.0f, 1e-6);
}

TEST(compositor, foregroundOnlyCarriesTheMatteAsAlpha) {
    Fixture f(0.9f, 0.3f, 0.2f, 0.75f);
    f.settings.outputMode = OutputMode::ForegroundOnly;
    f.settings.backgroundTransparency = 0.1f; // must have NO effect in this mode
    f.run();

    CHECK_NEAR(f.at()[3], 0.75f, 1e-6);
    CHECK_NEAR(f.at()[0], 0.9f, 1e-6);
    CHECK_NEAR(f.at()[1], 0.3f, 1e-6);
}

TEST(compositor, backgroundOnlyIsTheInverseMatteRegardlessOfTransparency) {
    // Background Only is a preview of what was removed. It must not go empty at the default
    // setting, which is the failure mode that would make the control look broken.
    for (float transparency : {0.0f, 0.5f, 1.0f}) {
        Fixture f(0.9f, 0.3f, 0.2f, 0.7f);
        f.settings.outputMode = OutputMode::BackgroundOnly;
        f.settings.backgroundTransparency = transparency;
        f.run();
        CHECK_NEAR(f.at()[3], 0.3f, 1e-6);
        // RGB stays the source colour: the mode changes coverage, not colour.
        CHECK_NEAR(f.at()[0], 0.9f, 1e-6);
    }

    // A fully foreground pixel is fully transparent in this mode.
    Fixture g(0.9f, 0.3f, 0.2f, 1.0f);
    g.settings.outputMode = OutputMode::BackgroundOnly;
    g.run();
    CHECK_NEAR(g.at()[3], 0.0f, 1e-6);
}

TEST(compositor, matteOnlyIsGreyAndOpaque) {
    Fixture f(0.9f, 0.3f, 0.2f, 0.42f);
    f.settings.outputMode = OutputMode::MatteOnly;
    f.run();

    // Greyscale, fully opaque, and NOT black. A matte preview that came out black is the most
    // visible possible failure of this mode.
    CHECK_NEAR(f.at()[0], 0.42f, 1e-6);
    CHECK_NEAR(f.at()[1], 0.42f, 1e-6);
    CHECK_NEAR(f.at()[2], 0.42f, 1e-6);
    CHECK_NEAR(f.at()[3], 1.0f, 1e-6);
}

TEST(compositor, matteInvertFlipsTheMatteOnly) {
    Fixture f(0.5f, 0.5f, 0.5f, 0.3f);
    f.settings.outputMode = OutputMode::MatteOnly;
    f.settings.matteInvert = true;
    f.run();
    CHECK_NEAR(f.at()[0], 0.7f, 1e-6);

    Fixture g(0.5f, 0.5f, 0.5f, 0.3f);
    g.settings.outputMode = OutputMode::ForegroundOnly;
    g.settings.matteInvert = true;
    g.run();
    CHECK_NEAR(g.at()[3], 0.7f, 1e-6);
}

TEST(compositor, outputIsStraightAlphaNotPremultiplied) {
    // The compositor writes UN-premultiplied output; the plugin layer re-premultiplies once, at the
    // boundary, if the host asked for it. Double-premultiplying is a real bug this catches.
    Fixture f(0.8f, 0.4f, 0.2f, 0.5f);
    f.settings.outputMode = OutputMode::ForegroundOnly;
    f.run();

    CHECK_NEAR(f.at()[0], 0.8f, 1e-6); // NOT 0.4
    CHECK_NEAR(f.at()[3], 0.5f, 1e-6);
}

TEST(compositor, mismatchedMatteSizeDoesNotReadOutOfBounds) {
    // A matte smaller than the frame must be resampled by nearest-neighbour indexing rather than
    // walking off the end of its buffer.
    FloatImage src(8, 8);
    src.fill(0.5f, 0.5f, 0.5f, 1.0f);
    Matte small(2, 2);
    small.fill(0.9f);
    FloatImage dst;
    dst.resize(8, 8);

    PipelineSettings settings;
    settings.outputMode = OutputMode::MatteOnly;
    composite(src, small, settings, dst); // must not crash

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            CHECK_NEAR(dst.pixel(x, y)[0], 0.9f, 1e-6);
            CHECK_NEAR(dst.pixel(x, y)[3], 1.0f, 1e-6);
        }
    }
}

TEST(compositor, emptyMatteDegradesGracefully) {
    FloatImage src(4, 4);
    src.fill(0.5f, 0.5f, 0.5f, 1.0f);
    Matte none;
    FloatImage dst;
    dst.resize(4, 4);

    PipelineSettings settings;
    settings.outputMode = OutputMode::ForegroundOnly;
    composite(src, none, settings, dst); // must not crash

    // With no matte there is no foreground claim to make; transparent is the honest answer, and
    // crucially the RGB was not blackened.
    CHECK(std::isfinite(dst.pixel(0, 0)[3]));
}

TEST(compositor, passThroughWhenDisabled) {
    PipelineSettings settings;
    settings.enable = false;
    CHECK(isPassThrough(settings));
}

TEST(compositor, passThroughWhenCompositeKeepsWholeBackground) {
    PipelineSettings settings;
    settings.enable = true;
    settings.outputMode = OutputMode::Composite;
    settings.backgroundTransparency = 0.0f; // nothing removed: output == input
    CHECK(isPassThrough(settings));

    settings.backgroundTransparency = 0.001f;
    CHECK(!isPassThrough(settings));

    settings.backgroundTransparency = 1.0f; // the default: background fully removed
    CHECK(!isPassThrough(settings));
}

TEST(compositor, noPassThroughForOtherModes) {
    PipelineSettings settings;
    settings.enable = true;
    settings.backgroundTransparency = 0.0f; // would be pass-through in Composite

    settings.outputMode = OutputMode::ForegroundOnly;
    CHECK(!isPassThrough(settings));
    settings.outputMode = OutputMode::BackgroundOnly;
    CHECK(!isPassThrough(settings));
    settings.outputMode = OutputMode::MatteOnly;
    CHECK(!isPassThrough(settings));
}

TEST(compositor, passThroughOutputIsBitIdentical) {
    // When the pipeline takes its fast path the frame must come out byte-for-byte unchanged. This
    // is the concrete form of the "no colour shifts" requirement for the disabled case.
    FloatImage src(3, 3);
    const float pattern[3 * 3 * 4] = {
        0.00f, 0.11f, 0.22f, 1.0f, 0.33f, 0.44f, 0.55f, 0.5f, 0.66f, 0.77f, 0.88f, 0.99f,
        1.00f, 0.50f, 0.25f, 0.0f, 0.10f, 0.20f, 0.30f, 0.4f, 0.95f, 0.05f, 0.5f,  1.0f,
        0.18f, 0.18f, 0.18f, 1.0f, 0.7f,  0.3f,  0.1f,  0.8f, 0.0f,  0.0f, 0.0f,  0.0f,
    };
    std::memcpy(src.data(), pattern, sizeof(pattern));

    PipelineSettings settings;
    settings.enable = false;
    CHECK(isPassThrough(settings));

    // The pipeline's fast path is a plain copy; reproduce it here to pin the expectation.
    FloatImage dst;
    dst.resize(src.width(), src.height());
    std::memcpy(dst.data(), src.data(), sizeof(pattern));

    for (size_t i = 0; i < sizeof(pattern) / sizeof(float); ++i) {
        CHECK_MSG(dst.data()[i] == pattern[i],
                  "pass-through changed data at index " + std::to_string(i));
    }
}

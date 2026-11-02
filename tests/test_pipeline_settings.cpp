// SPDX-License-Identifier: MIT
//
// PipelineSettings is the only type that crosses the OFX boundary, so its defaults are the plugin's
// out-of-the-box behaviour and sanitise() is the defence against a hand-edited project file or a
// malformed animated keyframe.

#include "TestCase.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "composite/Compositor.h"
#include "core/PipelineSettings.h"

using namespace aibgr;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
} // namespace

TEST(settings, defaultsDoTheJobWithoutAnyTweaking) {
    // What a freshly dragged-on effect must do: enabled, background removed, balanced quality,
    // composited output.
    PipelineSettings s;
    CHECK(s.enable);
    CHECK(s.outputMode == OutputMode::Composite);
    CHECK(s.quality == QualityPreset::Balanced512);
    CHECK(s.processingMode == ProcessingMode::Automatic);
    CHECK_NEAR(s.backgroundTransparency, 1.0f, 0.0); // background fully removed
    CHECK(!isPassThrough(s)); // the default really does something
    CHECK(s.model == ModelId::Auto);
    CHECK(s.inputColorSpace == ColorSpaceAssumption::Auto);
    CHECK(s.processingResolution == ProcessingResolutionMode::FollowHost);
    CHECK(s.gpuBackend == GpuBackend::Auto);
    CHECK(s.enableMatteCache);
    CHECK_NEAR(s.detailRefinement, 60.0f, 0.0);
    CHECK_NEAR(s.edgeFeather, 1.0f, 0.0);
    CHECK_NEAR(s.temporalStrength, 0.35f, 0.0);
    CHECK_NEAR(s.fallbackKeyTolerance, 0.25f, 0.0);
}

TEST(settings, qualityResolutionMapping) {
    CHECK_EQ(qualityResolution(QualityPreset::Draft256), 256);
    CHECK_EQ(qualityResolution(QualityPreset::Fast384), 384);
    CHECK_EQ(qualityResolution(QualityPreset::Balanced512), 512);
    CHECK_EQ(qualityResolution(QualityPreset::High768), 768);
    CHECK_EQ(qualityResolution(QualityPreset::Maximum1024), 1024);

    PipelineSettings s;
    s.quality = QualityPreset::High768;
    CHECK_EQ(s.matteResolution(), 768);
}

TEST(settings, sanitiseClampsEveryRange) {
    PipelineSettings s;
    s.despecklePx = -50;
    s.detailRefinement = 5000.0f;
    s.edgeFeather = -3.0f;
    s.edgeExpansion = 1e6f;
    s.edgeSmoothing = -1e6f;
    s.shiftMatte = 42.0f;
    s.backgroundTransparency = 7.0f;
    s.fallbackKeyTolerance = -2.0f;
    s.fallbackKeySoftness = 0.0f;
    s.gpuDeviceId = 99;
    s.gpuMemoryLimitMB = -1;
    s.maxConcurrentInference = 0;
    s.cacheSizeMB = 1 << 30;
    s.intraOpThreads = -8;
    s.temporalStrength = 3.0f;
    s.motionSensitivity = -3.0f;
    s.sanitise();

    CHECK_EQ(s.despecklePx, 0);
    CHECK_NEAR(s.detailRefinement, 100.0f, 0.0);
    CHECK_NEAR(s.edgeFeather, 0.0f, 0.0);
    CHECK_NEAR(s.edgeExpansion, 100.0f, 0.0);
    CHECK_NEAR(s.edgeSmoothing, 0.0f, 0.0);
    CHECK_NEAR(s.shiftMatte, 0.5f, 0.0);
    CHECK_NEAR(s.backgroundTransparency, 1.0f, 0.0);
    CHECK_NEAR(s.fallbackKeyTolerance, 0.0f, 0.0);
    CHECK(s.fallbackKeySoftness > 0.0f); // a zero softness would divide by zero in the smoothstep
    CHECK_EQ(s.gpuDeviceId, 7);
    CHECK_EQ(s.gpuMemoryLimitMB, 0);
    CHECK_EQ(s.maxConcurrentInference, 1);
    CHECK(s.cacheSizeMB <= 8192);
    CHECK_EQ(s.intraOpThreads, 0);
    CHECK_NEAR(s.temporalStrength, 1.0f, 0.0);
    CHECK_NEAR(s.motionSensitivity, 0.0f, 0.0);
}

TEST(settings, sanitiseSurvivesNaNAndInfinity) {
    // A host that hands us NaN must not produce a NaN matte: NaN propagates through every
    // comparison and would leave the frame unchanged or fully transparent, with no error anywhere.
    PipelineSettings s;
    s.detailRefinement = kNaN;
    s.edgeFeather = kInf;
    s.edgeExpansion = -kInf;
    s.edgeSmoothing = kNaN;
    s.shiftMatte = kNaN;
    s.backgroundTransparency = kNaN;
    s.fallbackKeyTolerance = kNaN;
    s.temporalStrength = kInf;
    s.motionSensitivity = kNaN;
    s.sanitise();

    CHECK(std::isfinite(s.detailRefinement));
    CHECK(std::isfinite(s.edgeFeather));
    CHECK(std::isfinite(s.edgeExpansion));
    CHECK(std::isfinite(s.edgeSmoothing));
    CHECK(std::isfinite(s.shiftMatte));
    CHECK(std::isfinite(s.backgroundTransparency));
    // A NaN backgroundTransparency falls back to the DEFAULT, which removes the background. If it
    // fell back to 0 the plugin would silently become a no-op.
    CHECK_NEAR(s.backgroundTransparency, 1.0f, 0.0);
    CHECK(std::isfinite(s.temporalStrength));
    CHECK(std::isfinite(s.motionSensitivity));
}

TEST(settings, sanitiseIsIdempotent) {
    PipelineSettings s;
    s.edgeFeather = 1e9f;
    s.shiftMatte = -1e9f;
    s.sanitise();
    const PipelineSettings once = s;
    s.sanitise();
    CHECK_NEAR(s.edgeFeather, once.edgeFeather, 0.0);
    CHECK_NEAR(s.shiftMatte, once.shiftMatte, 0.0);
}

TEST(settings, rawMatteHashTracksWhatTheSegmenterSees) {
    const PipelineSettings base;
    const uint64_t h = base.rawMatteHash();

    auto mutated = [&](auto fn) {
        PipelineSettings s = base;
        fn(s);
        return s.rawMatteHash();
    };

    CHECK(mutated([](PipelineSettings& s) { s.quality = QualityPreset::Maximum1024; }) != h);
    CHECK(mutated([](PipelineSettings& s) { s.model = ModelId::ViTMatte; }) != h);
    CHECK(mutated([](PipelineSettings& s) {
        s.processingMode = ProcessingMode::InvertRemoveSubject;
    }) != h);
    CHECK(mutated([](PipelineSettings& s) {
        s.inputColorSpace = ColorSpaceAssumption::LogC;
    }) != h);
    CHECK(mutated([](PipelineSettings& s) { s.despecklePx = 40; }) != h);
    CHECK(mutated([](PipelineSettings& s) { s.enable = false; }) != h);
}

TEST(settings, rawMatteHashIgnoresRefinementControls) {
    // The point of splitting the two hashes: scrubbing a feather slider must stay a cache hit for
    // the expensive segmentation stage.
    const PipelineSettings base;
    PipelineSettings s = base;
    s.edgeFeather += 3.0f;
    s.edgeExpansion += 2.0f;
    s.edgeSmoothing += 5.0f;
    s.detailRefinement += 10.0f;
    s.shiftMatte += 0.1f;
    s.sanitise();

    CHECK_EQ(s.rawMatteHash(), base.rawMatteHash());
    CHECK(s.refinementHash() != base.refinementHash());
}

TEST(settings, refinementHashSeedsFromTheRawHash) {
    PipelineSettings a;
    PipelineSettings b = a;
    b.quality = QualityPreset::Draft256;

    // A quality change alters the raw matte, so it must also invalidate any temporal state built on
    // top of it - which it does, because refinementHash is seeded from rawMatteHash.
    CHECK(a.rawMatteHash() != b.rawMatteHash());
    CHECK(a.refinementHash() != b.refinementHash());
}

TEST(settings, enumNamesAreDistinctAndNonNull) {
    const char* qualities[] = {
        qualityName(QualityPreset::Draft256),   qualityName(QualityPreset::Fast384),
        qualityName(QualityPreset::Balanced512), qualityName(QualityPreset::High768),
        qualityName(QualityPreset::Maximum1024),
    };
    for (int i = 0; i < 5; ++i) {
        CHECK(qualities[i] != nullptr && qualities[i][0] != '\0');
        for (int j = i + 1; j < 5; ++j) {
            CHECK_MSG(std::strcmp(qualities[i], qualities[j]) != 0, "duplicate quality name");
        }
    }

    CHECK(gpuBackendName(GpuBackend::Cuda) != nullptr);
    CHECK(modelIdName(ModelId::BirefNetLite) != nullptr);
    CHECK(premultiplicationName(Premultiplication::Straight) != nullptr);
}

// SPDX-License-Identifier: MIT
#include "TestCase.h"
#include "inference/U2NetSegmenter.h"
#include <cmath>
#include <cstdlib>

using namespace aibgr;

TEST(u2net, missingAssetsReturnAnError) {
    U2NetSegmenter ai;
    CHECK(!ai.open("missing-runtime", "missing-model").ok());
    FloatImage frame(32, 32); Matte matte;
    CHECK(!ai.acquire(frame, matte).ok());
}

// Optional live integration test: point at independently downloaded runtime and model. We do not
// ship large binaries/weights and ordinary builds remain offline and do not require AI assets.
TEST(u2net, realCpuRuntimeWhenSupplied) {
    const char* runtime = std::getenv("AIBGR_TEST_ORT");
    const char* model = std::getenv("AIBGR_TEST_MODEL");
    if (!runtime || !model) return;
    U2NetSegmenter ai;
    Result opened = ai.open(runtime, model);
    CHECK_MSG(opened.ok(), opened.toString());
    if (!opened.ok()) return;
    FloatImage image(320, 160);
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        float* p = image.pixel(x, y);
        const bool subject = x > 95 && x < 225 && y > 30 && y < 135;
        p[0] = subject ? 0.85f : 0.05f;
        p[1] = subject ? 0.25f : 0.48f;
        p[2] = subject ? 0.12f : 0.75f;
        p[3] = 1.0f;
    }
    Matte matte;
    Result result = ai.acquire(image, matte);
    CHECK_MSG(result.ok(), result.toString());
    if (!result.ok()) return;
    CHECK_EQ(matte.width(), image.width());
    CHECK_EQ(matte.height(), image.height());
    float lo = 1.0f, hi = 0.0f;
    for (size_t i = 0; i < matte.floatCount(); ++i) {
        CHECK(std::isfinite(matte.data()[i]));
        lo = std::min(lo, matte.data()[i]); hi = std::max(hi, matte.data()[i]);
    }
    CHECK(lo >= 0.0f && hi <= 1.0f);
    CHECK(hi - lo > 0.1f);
    // Golden values from the same ONNX model using the independent Python ORT API. This
    // asymmetric image also checks the bottom-up OFX to top-down model orientation.
    FloatImage golden(320, 320);
    for (int y = 0; y < 320; ++y) for (int x = 0; x < 320; ++x) {
        float* p = golden.pixel(x, y);
        bool subject = x >= 96 && x < 225 && y >= 30 && y < 160;
        p[0] = subject ? 0.85f : 0.05f;
        p[1] = subject ? 0.25f : 0.48f;
        p[2] = subject ? 0.12f : 0.75f;
        p[3] = 1.0f;
    }
    Matte reference;
    CHECK(ai.acquire(golden, reference).ok());
    CHECK_NEAR(reference.at(160, 80), 0.999999, 0.01);
    CHECK_NEAR(reference.at(160, 220), 0.000333, 0.01);
    CHECK_NEAR(reference.at(100, 155), 0.997671, 0.02);
    Matte again;
    CHECK(ai.acquire(image, again).ok());
    CHECK_NEAR(matte.at(160, 80), again.at(160, 80), 1e-5);
    const char* invalid = std::getenv("AIBGR_TEST_BAD_MODEL");
    if (invalid) CHECK(!ai.open(runtime, invalid).ok());
}

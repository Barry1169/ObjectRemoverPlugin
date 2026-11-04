// SPDX-License-Identifier: MIT
#include "TestCase.h"
#include "guidance/CorrectionMask.h"
#include "core/Pipeline.h"
#include <string>
using namespace aibgr;

namespace {
std::string fixture(const char* name) { return std::string(AIBGR_TEST_DATA_DIR) + "/" + name; }
class ConstantMatte final : public MatteSource {
public:
    Result acquire(const FloatImage& image, Matte& out) override {
        out.resize(image.width(), image.height()); out.fill(0.5f); return Result::success();
    }
    const char* name() const noexcept override { return "constant"; }
};
}

TEST(guidance, pngOverridesCorrectPixelsAtFullResolution) {
    CorrectionMask mask; bool changed = false;
    CHECK(mask.update(fixture("guidance.png"), changed).ok()); CHECK(changed); CHECK(mask.ready());
    CHECK(mask.update(fixture("guidance.png"), changed).ok()); CHECK(!changed);
    Matte matte(64,64); matte.fill(0.5f);
    CHECK(mask.apply(matte).ok());
    // PNG top-left green, top-right red; OFX bottom-left coordinate system.
    CHECK_NEAR(matte.at(10, 54), 1.0, 1e-6);
    CHECK_NEAR(matte.at(54, 54), 0.0, 1e-6);
    CHECK_NEAR(matte.at(10, 10), 0.5 + 0.5 * 128.0 / 255.0, 1e-6);
    CHECK_NEAR(matte.at(54, 10), 0.5, 1e-6);
    Matte wrong(63,64); wrong.fill(0.5f);
    CHECK(!mask.apply(wrong).ok()); CHECK_NEAR(wrong.at(1,1), 0.5, 1e-6);
}
TEST(guidance, badAndMissingFilesAreIgnoredAndRecover) {
    CorrectionMask mask; bool changed = false;
    CHECK(!mask.update(fixture("missing.png"), changed).ok()); CHECK(changed); CHECK(!mask.ready());
    CHECK(!mask.update(fixture("missing.png"), changed).ok()); CHECK(!changed);
    CHECK(!mask.update(fixture("invalid.png"), changed).ok()); CHECK(changed); CHECK(!mask.ready());
    CHECK(!mask.update(fixture("opaque.png"), changed).ok()); CHECK(changed); CHECK(!mask.ready());
    CHECK(mask.update(fixture("guidance.png"), changed).ok()); CHECK(mask.ready());
    CHECK(mask.update("", changed).ok()); CHECK(!mask.ready());
}
TEST(guidance, correctionIsAppliedBeforeCompositing) {
    CorrectionMask mask; bool changed = false;
    CHECK(mask.update(fixture("guidance.png"), changed).ok());
    ConstantMatte source; Pipeline pipeline; pipeline.setMatteSource(&source);
    pipeline.setMatteOverride(&mask);
    FloatImage input(64,64), output; input.fill(0.3f,0.4f,0.5f,1.0f);
    PipelineSettings settings; settings.outputMode = OutputMode::MatteOnly;
    FrameContext ctx; PipelineStats stats;
    CHECK(pipeline.process(ctx, settings, input, output, stats).ok());
    CHECK_NEAR(output.pixel(10,54)[0], 1.0, 1e-6);
    CHECK_NEAR(output.pixel(54,54)[0], 0.0, 1e-6);
    CHECK_NEAR(output.pixel(54,10)[0], 0.5, 1e-6);
    settings.matteInvert = true;
    CHECK(pipeline.process(ctx, settings, input, output, stats).ok());
    CHECK_NEAR(output.pixel(10,54)[0], 1.0, 1e-6);
    CHECK_NEAR(output.pixel(54,54)[0], 0.0, 1e-6);
}

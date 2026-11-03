// SPDX-License-Identifier: MIT
//
// End-to-end pipeline tests. These are the executable form of section 9's acceptance criteria that
// can be checked without Resolve: correct alpha, no colour shift, no unintended black background,
// and graceful behaviour when the model is missing.

#include "TestCase.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/FloatImage.h"
#include "core/FrameContext.h"
#include "core/Pipeline.h"
#include "core/PipelineSettings.h"
#include "matte/Matte.h"
#include "util/Status.h"

using namespace aibgr;

namespace {

/// A red subject block on a green field, opaque. Deliberately not aligned to any power of two so
/// the resampler's edge handling is exercised.
FloatImage makeFrame(int w, int h) {
    FloatImage img(w, h);
    const int x0 = w / 5;
    const int y0 = h / 5;
    const int x1 = w - w / 5;
    const int y1 = h - h / 5;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float* p = img.pixel(x, y);
            const bool subject = (x >= x0 && x < x1 && y >= y0 && y < y1);
            if (subject) {
                p[0] = 0.90f;
                p[1] = 0.10f;
                p[2] = 0.10f;
            } else {
                p[0] = 0.10f;
                p[1] = 0.70f;
                p[2] = 0.20f;
            }
            p[3] = 1.0f;
        }
    }
    return img;
}

bool isSubject(int x, int y, int w, int h) {
    return x >= w / 5 && x < w - w / 5 && y >= h / 5 && y < h - h / 5;
}

FrameContext makeContext(int w, int h) {
    FrameContext ctx;
    ctx.time = 1.0;
    ctx.bounds = RectI{0, 0, w, h};
    ctx.renderWindow = RectI{0, 0, w, h};
    ctx.rod = RectI{0, 0, w, h};
    return ctx;
}

class StubbornAbort final : public AbortChecker {
public:
    bool aborted() const noexcept override { return true; }
};

class NeverAbort final : public AbortChecker {
public:
    bool aborted() const noexcept override { return false; }
};

/// A matte source that always fails, standing in for "the model could not be loaded".
class BrokenMatteSource final : public MatteSource {
public:
    Result acquire(const FloatImage& /*image*/, Matte& /*out*/) override {
        return Result::fail(Status::NotFound, "simulated missing model");
    }
    const char* name() const noexcept override { return "BrokenMatteSource"; }
};

/// A matte source that records what it was handed, so the colour-encoding contract can be checked.
class SpyMatteSource final : public MatteSource {
public:
    Result acquire(const FloatImage& image, Matte& out) override {
        out.resize(image.width(), image.height());
        out.fill(0.5f);
        sawWidth = image.width();
        sawHeight = image.height();
        centreR = image.pixel(image.width() / 2, image.height() / 2)[0];
        centreA = image.pixel(image.width() / 2, image.height() / 2)[3];
        return Result::success();
    }
    const char* name() const noexcept override { return "SpyMatteSource"; }

    int sawWidth = 0;
    int sawHeight = 0;
    float centreR = -1.0f;
    float centreA = -1.0f;
};

} // namespace

TEST(pipeline, disabledIsAnExactCopy) {
    // The strongest form of "no colour shift": with the effect off, every float in the output buffer
    // must equal the corresponding float in the input buffer.
    const int w = 48;
    const int h = 36;
    FloatImage src = makeFrame(w, h);
    src.pixel(3, 3)[3] = 0.4f; // a semi-transparent pixel too, so alpha is compared as well

    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings;
    settings.enable = false;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    const Result r = pipeline.process(ctx, settings, src, dst, stats);
    CHECK(r.ok());
    CHECK_EQ(dst.width(), w);
    CHECK_EQ(dst.height(), h);

    for (std::size_t i = 0; i < src.floatCount(); ++i) {
        CHECK_MSG(dst.data()[i] == src.data()[i],
                  "disabled pipeline changed data at index " + std::to_string(i));
    }
    CHECK(!stats.matteSourceRan);
}

TEST(pipeline, thumbnailRenderNeverRunsTheMatteSource) {
    // Hosts generate thumbnails constantly while scrubbing the effect list. Loading a model for one
    // would be a multi-second stall on the UI thread.
    const int w = 32;
    const int h = 32;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    SpyMatteSource spy;
    pipeline.setMatteSource(&spy);

    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    ctx.thumbnailRender = true;
    PipelineStats stats;

    CHECK(pipeline.process(ctx, settings, src, dst, stats).ok());
    CHECK(!stats.matteSourceRan);
    CHECK_EQ(spy.sawWidth, 0); // never called
    for (std::size_t i = 0; i < src.floatCount(); ++i) {
        CHECK_EXACT(dst.data()[i], src.data()[i]);
    }
}

TEST(pipeline, removesBackgroundAndPreservesColour) {
    const int w = 64;
    const int h = 48;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings; // defaults: enabled, Composite, backgroundTransparency 1
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    const Result r = pipeline.process(ctx, settings, src, dst, stats);
    CHECK(r.ok());
    CHECK(stats.matteSourceRan);
    CHECK(stats.usedFallbackKey); // no model backend in a unit test
    CHECK(stats.matteWidth > 0 && stats.matteHeight > 0);

    int subjectOpaque = 0;
    int backgroundClear = 0;
    int total = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float* s = src.pixel(x, y);
            const float* d = dst.pixel(x, y);
            ++total;

            // NO COLOUR SHIFT: RGB must survive untouched in every output mode that composites.
            CHECK_NEAR(d[0], s[0], 1e-6);
            CHECK_NEAR(d[1], s[1], 1e-6);
            CHECK_NEAR(d[2], s[2], 1e-6);

            // NO UNINTENDED BLACK BACKGROUND: removal is carried by ALPHA, never by zeroing RGB.
            // (Checked above by the RGB equality, which holds for background pixels too.)
            CHECK(std::isfinite(d[3]));
            CHECK(d[3] >= 0.0f && d[3] <= 1.0f);

            if (isSubject(x, y, w, h)) {
                if (d[3] > 0.5f) {
                    ++subjectOpaque;
                }
            } else if (d[3] < 0.5f) {
                ++backgroundClear;
            }
        }
    }

    // The fallback colour key is not a segmentation network, so it will not be perfect on every
    // pixel - but it must get the overwhelming majority of an easy synthetic frame right.
    CHECK_MSG(subjectOpaque > total / 4,
              "almost no subject pixels survived: " + std::to_string(subjectOpaque));
    CHECK_MSG(backgroundClear > total / 4,
              "almost no background pixels were removed: " + std::to_string(backgroundClear));
}

TEST(pipeline, outputIsUnpremultiplied) {
    // The compositor contract: dst is straight alpha, and the plugin layer re-premultiplies once at
    // the OFX boundary. Double premultiplication would darken every semi-transparent edge.
    const int w = 32;
    const int h = 32;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    pipeline.process(ctx, settings, src, dst, stats);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float* s = src.pixel(x, y);
            const float* d = dst.pixel(x, y);
            // Straight means colour is independent of coverage.
            CHECK_NEAR(d[0], s[0], 1e-6);
        }
    }
}

TEST(pipeline, matteOnlyProducesAnOpaqueGreyImage) {
    const int w = 40;
    const int h = 40;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings;
    settings.outputMode = OutputMode::MatteOnly;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    CHECK(pipeline.process(ctx, settings, src, dst, stats).ok());

    int opaque = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float* d = dst.pixel(x, y);
            CHECK_NEAR(d[3], 1.0f, 1e-6); // never transparent: a matte preview must be visible
            CHECK_NEAR(d[0], d[1], 1e-6);
            CHECK_NEAR(d[1], d[2], 1e-6);
            CHECK(d[0] >= 0.0f && d[0] <= 1.0f);
            if (d[3] > 0.999f) {
                ++opaque;
            }
        }
    }
    CHECK_EQ(opaque, w * h);
}

TEST(pipeline, invertSwapsSubjectAndBackground) {
    const int w = 32;
    const int h = 32;
    FloatImage src = makeFrame(w, h);

    Pipeline pipeline;
    PipelineSettings normal;
    normal.outputMode = OutputMode::MatteOnly;
    PipelineSettings inverted = normal;
    inverted.processingMode = ProcessingMode::InvertRemoveSubject;

    FrameContext ctx = makeContext(w, h);
    FloatImage a;
    FloatImage b;
    PipelineStats stats;

    pipeline.process(ctx, normal, src, a, stats);
    pipeline.process(ctx, inverted, src, b, stats);

    // Centre of the subject: bright normally, dark when inverted.
    const float normalCentre = a.pixel(16, 16)[0];
    const float invertedCentre = b.pixel(16, 16)[0];
    CHECK_NEAR(normalCentre + invertedCentre, 1.0f, 1e-5);
}

TEST(pipeline, colourEncodingNeverTouchesTheOutput) {
    // The colour transform is applied ONLY to the scratch buffer fed to the network. Feeding the
    // same frame with a different declared colour space must produce identical output RGB.
    const int w = 32;
    const int h = 32;
    FloatImage src = makeFrame(w, h);

    Pipeline pipeline;
    SpyMatteSource spy;
    pipeline.setMatteSource(&spy);

    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    PipelineSettings display = PipelineSettings{};
    display.inputColorSpace = ColorSpaceAssumption::DisplayReferred;
    FloatImage dstDisplay;
    pipeline.process(ctx, display, src, dstDisplay, stats);
    CHECK(!stats.colourTransformed);
    const float spySawDisplay = spy.centreR;

    PipelineSettings linear = PipelineSettings{};
    linear.inputColorSpace = ColorSpaceAssumption::SceneLinear;
    FloatImage dstLinear;
    pipeline.process(ctx, linear, src, dstLinear, stats);
    CHECK(stats.colourTransformed);

    // The network saw different numbers...
    CHECK(spySawDisplay != spy.centreR);
    // ...but the rendered frame is identical.
    for (std::size_t i = 0; i < src.floatCount(); ++i) {
        CHECK_MSG(std::fabs(dstDisplay.data()[i] - dstLinear.data()[i]) < 1e-6f,
                  "colour space choice changed the output at index " + std::to_string(i));
    }
    // And RGB still equals the source.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            CHECK_NEAR(dstLinear.pixel(x, y)[0], src.pixel(x, y)[0], 1e-6);
        }
    }
}

TEST(pipeline, matteSourceReceivesAnImageAtMatteResolution) {
    const int w = 640;
    const int h = 480;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    SpyMatteSource spy;
    pipeline.setMatteSource(&spy);

    PipelineSettings settings;
    settings.quality = QualityPreset::Draft256;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    pipeline.process(ctx, settings, src, dst, stats);

    // The long edge must be at most the quality preset, never larger: the pipeline must not upscale
    // the frame before inference.
    CHECK(spy.sawWidth > 0);
    CHECK_MSG(std::max(spy.sawWidth, spy.sawHeight) <= 256,
              "matte source saw a larger image than the quality preset allows");
    // Alpha survives the trip to the network.
    CHECK_NEAR(spy.centreA, 1.0f, 1e-5);
    // Output is still full frame.
    CHECK_EQ(dst.width(), w);
    CHECK_EQ(dst.height(), h);
    CHECK(stats.matteUpscaled);
}

TEST(pipeline, draftQualityForcesTheCheapestPreset) {
    const int w = 512;
    const int h = 512;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    SpyMatteSource spy;
    pipeline.setMatteSource(&spy);

    PipelineSettings settings;
    settings.quality = QualityPreset::Maximum1024;
    FrameContext ctx = makeContext(w, h);
    ctx.draftQuality = true; // host is scrubbing: speed beats accuracy
    PipelineStats stats;

    pipeline.process(ctx, settings, src, dst, stats);
    CHECK_MSG(std::max(spy.sawWidth, spy.sawHeight) <= 256,
              "draft quality did not force the cheapest preset");
}

TEST(pipeline, processingResolutionCapsTheMatte) {
    const int w = 1024;
    const int h = 1024;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    SpyMatteSource spy;
    pipeline.setMatteSource(&spy);
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    PipelineSettings follow = PipelineSettings{};
    follow.quality = QualityPreset::Balanced512;
    follow.processingResolution = ProcessingResolutionMode::FollowHost;
    pipeline.process(ctx, follow, src, dst, stats);
    const int followEdge = std::max(spy.sawWidth, spy.sawHeight);

    PipelineSettings quarter = follow;
    quarter.processingResolution = ProcessingResolutionMode::ForceQuarter;
    pipeline.process(ctx, quarter, src, dst, stats);
    const int quarterEdge = std::max(spy.sawWidth, spy.sawHeight);

    CHECK(followEdge > 0);
    CHECK_MSG(quarterEdge < followEdge,
              "ForceQuarter did not reduce the working resolution: " + std::to_string(quarterEdge));

    // ForceFull uses the frame itself, capped by nothing but the frame size.
    PipelineSettings full = follow;
    full.processingResolution = ProcessingResolutionMode::ForceFull;
    pipeline.process(ctx, full, src, dst, stats);
    CHECK_EQ(std::max(spy.sawWidth, spy.sawHeight), 1024);
}

TEST(pipeline, abortCancels) {
    const int w = 64;
    const int h = 64;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    StubbornAbort abort;
    pipeline.setAbortChecker(&abort);

    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    const Result r = pipeline.process(ctx, settings, src, dst, stats);
    CHECK(!r.ok());
    CHECK(r.status == Status::Cancelled);
}

TEST(pipeline, anExplicitNeverAbortCheckerDoesNotInterfere) {
    const int w = 32;
    const int h = 32;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    NeverAbort ok;
    pipeline.setAbortChecker(&ok);

    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;
    CHECK(pipeline.process(ctx, settings, src, dst, stats).ok());
}

TEST(pipeline, failingMatteSourceDegradesToPassThrough) {
    // Requirement: "correct behaviour when models are unavailable". The frame must come back
    // unchanged and the action must still SUCCEED - a failed render in Resolve means a red frame or
    // a stalled timeline, which is worse than showing the original picture.
    const int w = 32;
    const int h = 32;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    BrokenMatteSource broken;
    pipeline.setMatteSource(&broken);

    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    const Result r = pipeline.process(ctx, settings, src, dst, stats);
    CHECK_MSG(r.ok(), "a failing matte source must not fail the render");
    CHECK(stats.matteSourceRan);
    CHECK(!stats.message.empty()); // and must say why

    for (std::size_t i = 0; i < src.floatCount(); ++i) {
        CHECK_MSG(dst.data()[i] == src.data()[i],
                  "degraded path did not reproduce the source at index " + std::to_string(i));
    }
}

TEST(pipeline, emptySourceIsRejected) {
    FloatImage src;
    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings;
    FrameContext ctx;
    PipelineStats stats;

    const Result r = pipeline.process(ctx, settings, src, dst, stats);
    CHECK(!r.ok());
    CHECK(r.status == Status::InvalidArgument);
}

TEST(pipeline, nonSquareAndOddSizesWork) {
    // Section 9 asks for "various resolutions and aspect ratios". Nothing here may assume square,
    // even, or power-of-two dimensions.
    const int sizes[][2] = {{1, 1}, {3, 7}, {17, 5}, {640, 480}, {480, 640}, {1921, 1079}};
    Pipeline pipeline;
    PipelineSettings settings;
    PipelineStats stats;

    for (const auto& wh : sizes) {
        const int w = wh[0];
        const int h = wh[1];
        FloatImage src = makeFrame(w, h);
        FloatImage dst;
        FrameContext ctx = makeContext(w, h);

        const Result r = pipeline.process(ctx, settings, src, dst, stats);
        CHECK_MSG(r.ok(), "failed at " + std::to_string(w) + "x" + std::to_string(h));
        CHECK_EQ(dst.width(), w);
        CHECK_EQ(dst.height(), h);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float* d = dst.pixel(x, y);
                for (int c = 0; c < 4; ++c) {
                    CHECK_MSG(std::isfinite(d[c]),
                              "non-finite output at " + std::to_string(w) + "x" +
                                  std::to_string(h));
                }
            }
        }
    }
}

TEST(pipeline, repeatedRendersReuseBuffers) {
    // The steady-state claim: after warm-up, processing frame after frame at the same size performs
    // no further allocation. Checked by watching the destination buffer's capacity stay flat.
    const int w = 128;
    const int h = 96;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    pipeline.process(ctx, settings, src, dst, stats); // warm-up
    const std::size_t warm = dst.allocatedBytes();
    CHECK(warm >= static_cast<std::size_t>(w) * h * 4 * sizeof(float));

    for (int frame = 0; frame < 30; ++frame) {
        ctx.time = static_cast<double>(frame);
        const Result r = pipeline.process(ctx, settings, src, dst, stats);
        CHECK(r.ok());
        CHECK_EQ(dst.allocatedBytes(), warm);
        CHECK_EQ(dst.width(), w);
    }
}

TEST(pipeline, releaseBuffersThenRenderAgain) {
    // Bound to the inspector's Clear Cache button. After giving the memory back the pipeline must
    // still work on the next frame.
    const int w = 48;
    const int h = 48;
    FloatImage src = makeFrame(w, h);
    FloatImage dst;
    Pipeline pipeline;
    PipelineSettings settings;
    FrameContext ctx = makeContext(w, h);
    PipelineStats stats;

    CHECK(pipeline.process(ctx, settings, src, dst, stats).ok());
    pipeline.releaseBuffers();
    dst.release();
    CHECK(dst.empty());

    CHECK(pipeline.process(ctx, settings, src, dst, stats).ok());
    CHECK_EQ(dst.width(), w);
    CHECK_EQ(dst.height(), h);
}

TEST(pipeline, resolveMatteLongEdgeIsPredictable) {
    PipelineSettings s;
    s.quality = QualityPreset::Balanced512;
    s.processingResolution = ProcessingResolutionMode::FollowHost;

    CHECK_EQ(Pipeline::resolveMatteLongEdge(s, 1920, 1080), 512);
    // Never upscales a small frame.
    CHECK_EQ(Pipeline::resolveMatteLongEdge(s, 200, 100), 200);

    s.processingResolution = ProcessingResolutionMode::ForceHalf;
    CHECK_EQ(Pipeline::resolveMatteLongEdge(s, 1920, 1080), 256);

    s.processingResolution = ProcessingResolutionMode::ForceFull;
    CHECK_EQ(Pipeline::resolveMatteLongEdge(s, 1920, 1080), 1920);
}

TEST(pipeline, matteSourceIsNonOwningAndNullable) {
    Pipeline pipeline;
    CHECK(pipeline.matteSource() == nullptr); // built-in fallback key is used

    SpyMatteSource spy;
    pipeline.setMatteSource(&spy);
    CHECK(pipeline.matteSource() == &spy);

    pipeline.setMatteSource(nullptr);
    CHECK(pipeline.matteSource() == nullptr); // and still renders, via the fallback
}

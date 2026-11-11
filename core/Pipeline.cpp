// SPDX-License-Identifier: MIT
#include "core/Pipeline.h"

#include "composite/Compositor.h"
#include "matte/Resize.h"
#include "util/Log.h"
#include "util/ScopeTimer.h"

#include <algorithm>
#include <cstring>

namespace aibgr {

namespace {

/// Models with a patchified or windowed backbone need their input aligned to a grid. BiRefNet's
/// Swin backbone is the case we care about; 32 is a safe multiple for it and for the smaller
/// matting nets. A future MatteSource may declare its own value.
constexpr int kMatteAlignment = 32;

void copyImage(const FloatImage& src, FloatImage& dst) {
    dst.resize(src.width(), src.height());
    const std::size_t n = src.floatCount();
    if (n > 0) {
        std::memcpy(dst.data(), src.data(), n * sizeof(float));
    }
}

void invertMatte(Matte& m) noexcept {
    const std::size_t n = m.floatCount();
    float* p = m.data();
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = 1.0f - p[i];
    }
}

void shiftMatte(Matte& m, float delta) noexcept {
    const std::size_t n = m.floatCount();
    float* p = m.data();
    for (std::size_t i = 0; i < n; ++i) {
        p[i] = std::min(1.0f, std::max(0.0f, p[i] + delta));
    }
}

} // namespace

Result FallbackKeyMatteSource::acquire(const FloatImage& image, Matte& out) {
    if (image.empty()) {
        return Result::fail(Status::InvalidArgument, "fallback key: empty image");
    }
    out.resize(image.width(), image.height());
    computeFallbackKeyMatte(image, out, settings_);
    return Result::success();
}

Pipeline::Pipeline() : fallback_(std::make_unique<FallbackKeyMatteSource>()) {}

Pipeline::~Pipeline() = default;

bool Pipeline::aborted() const noexcept {
    return abortChecker_ != nullptr && abortChecker_->aborted();
}

int Pipeline::resolveMatteLongEdge(const PipelineSettings& settings, int frameWidth,
                                  int frameHeight) noexcept {
    int cap = settings.matteResolution();
    switch (settings.processingResolution) {
    case ProcessingResolutionMode::FollowHost:
        break; // cap is the quality preset
    case ProcessingResolutionMode::ForceHalf:
        cap = std::max(64, cap / 2);
        break;
    case ProcessingResolutionMode::ForceQuarter:
        cap = std::max(64, cap / 4);
        break;
    case ProcessingResolutionMode::ForceFull:
        cap = std::max(frameWidth, frameHeight);
        break;
    }
    return effectiveMatteResolution(std::max(frameWidth, frameHeight), cap);
}

void Pipeline::computeMatteSize(const PipelineSettings& settings, int srcW, int srcH, int& outW,
                                int& outH) noexcept {
    const int longEdge = resolveMatteLongEdge(settings, srcW, srcH);
    fitAndAlign(srcW, srcH, longEdge, kMatteAlignment, outW, outH);
}

Result Pipeline::process(const FrameContext& ctx, const PipelineSettings& settings,
                         const FloatImage& srcStraight, FloatImage& dst, PipelineStats& stats) {
    ScopeTimer total;
    stats = PipelineStats{};

    if (srcStraight.empty()) {
        return Result::fail(Status::InvalidArgument, "pipeline: empty source image");
    }

    const int w = srcStraight.width();
    const int h = srcStraight.height();
    dst.resize(w, h);

    // --- Fast paths -------------------------------------------------------
    // A thumbnail render must never touch a model: hosts generate thumbnails constantly while
    // scrubbing the effect list, and loading a 500 MB network for one is pure waste.
    if (ctx.thumbnailRender || isPassThrough(settings) || (selectionOnly_ && !selectionRefiner_)) {
        copyImage(srcStraight, dst);
        stats.totalMs = total.elapsedMs();
        stats.message = ctx.thumbnailRender ? "thumbnail: pass-through" : "pass-through";
        return Result::success();
    }

    // --- 1. Colour encoding for inference only ----------------------------
    // The composited output is derived from srcStraight, never from this buffer, so no choice here
    // can shift the rendered colours.
    PipelineSettings effective = settings;
    if (ctx.draftQuality && effective.quality > QualityPreset::Draft256) {
        effective.quality = QualityPreset::Draft256;
    }

    ColorSpaceAssumption cs = effective.inputColorSpace;
    if (cs == ColorSpaceAssumption::Auto) {
        cs = autoDetect(srcStraight);
    }
    stats.appliedColorSpace = cs;

    const FloatImage* encoded = &srcStraight;
    if (cs != ColorSpaceAssumption::DisplayReferred) {
        copyImage(srcStraight, encodedFull_);
        toInferenceEncoding(encodedFull_, cs);
        encoded = &encodedFull_;
        stats.colourTransformed = true;
    }

    // --- 2. Matte resolution ----------------------------------------------
    int mw = 0;
    int mh = 0;
    computeMatteSize(effective, w, h, mw, mh);
    stats.matteWidth = mw;
    stats.matteHeight = mh;

    // --- 3. Downscale ------------------------------------------------------
    const FloatImage* inferenceImage = encoded;
    if (mw != w || mh != h) {
        inferenceImg_.resize(mw, mh);
        resizeArea(*encoded, inferenceImg_);
        inferenceImage = &inferenceImg_;
    }

    if (aborted()) {
        return Result::fail(Status::Cancelled, "aborted before segmentation");
    }

    // --- 4. Raw matte ------------------------------------------------------
    {
        ScopeTimer t;
        MatteSource* source = (matteSource_ != nullptr) ? matteSource_ : fallback_.get();
        fallback_->setSettings({effective.fallbackKeyTolerance, effective.fallbackKeySoftness, 4});
        rawMatte_.resize(mw, mh);
        Result st = Result::success();
        if (selectionOnly_) rawMatte_.fill(0.0f);
        else st = source->acquire(*inferenceImage, rawMatte_);
        stats.matteSourceMs = t.elapsedMs();
        stats.matteSourceRan = !selectionOnly_;
        stats.usedFallbackKey = !selectionOnly_ && (matteSource_ == nullptr);
        if (!st.ok()) {
            stats.message = st.toString();
            AIBGR_LOGE("matte source '%s' failed: %s", source->name(), st.message.c_str());
            // Degrade rather than fail: hand the host the original frame.
            copyImage(srcStraight, dst);
            stats.totalMs = total.elapsedMs();
            return Result::success();
        }
    }

    // --- 5. Processing mode ------------------------------------------------
    if (!selectionOnly_ && effective.processingMode == ProcessingMode::InvertRemoveSubject) {
        invertMatte(rawMatte_);
    }
    // ProcessingMode::LargestSubjectOnly / AllSubjects are implemented by matte/Components in
    // Phase 5. Until then they behave as Automatic, which is the safe default.

    // --- 6. Global matte bias ---------------------------------------------
    if (!selectionOnly_ && effective.shiftMatte != 0.0f) {
        shiftMatte(rawMatte_, effective.shiftMatte);
    }

    // --- 7. Refinement -----------------------------------------------------
    {
        ScopeTimer t;
        // Phase 5 fills this in, in this exact order (docs/02-architecture.md section 3, step 4g):
        //   Components / despeckle (effective.despecklePx)
        //   GuidedFilter detail transfer (effective.detailRefinement), guide = srcStraight
        //   Morphology expansion (effective.edgeExpansion)
        //   Feather (effective.edgeFeather)
        //   Edge-aware smoothing (effective.edgeSmoothing)
        //   optional ViTMatte stage 2
        // Phase 6 inserts the temporal stabiliser here, gated on ctx.sequentialRenderStatus.
        rawMatte_.clamp01();
        stats.refineMs = t.elapsedMs();
    }

    if (aborted()) {
        return Result::fail(Status::Cancelled, "aborted after segmentation");
    }

    // --- 8. Upscale the matte back to frame resolution ---------------------
    Matte* finalMatte = &rawMatte_;
    if (mw != w || mh != h) {
        fullMatte_.resize(w, h);
        resizeBicubic(rawMatte_, fullMatte_);
        finalMatte = &fullMatte_;
        stats.matteUpscaled = true;
    }

    if (selectionRefiner_) {
        const Result selected = selectionRefiner_->refine(*encoded, *finalMatte, effective.matteInvert);
        if (!selected.ok()) {
            AIBGR_LOGE("guided selection: %s", selected.toString().c_str());
            if (selectionOnly_) {
                copyImage(srcStraight, dst);
                stats.message = "selection failed: pass-through";
                stats.totalMs = total.elapsedMs();
                return Result::success();
            }
        }
    }

    // Apply manual corrections at full resolution, so thin painted strokes are not
    // reduced to the AI model's 320-pixel input. On mismatch leave the base matte intact.
    if (matteOverride_) {
        const Result corrected = matteOverride_->apply(*finalMatte, effective.matteInvert);
        if (!corrected.ok()) AIBGR_LOGE("guidance mask: %s", corrected.toString().c_str());
    }

    if (brushOverride_) {
        const Result painted = brushOverride_->apply(*finalMatte, effective.matteInvert);
        if (!painted.ok()) AIBGR_LOGE("brush correction: %s", painted.toString().c_str());
    }

    // --- 9. Composite ------------------------------------------------------
    {
        ScopeTimer t;
        composite(srcStraight, *finalMatte, effective, dst);
        stats.compositeMs = t.elapsedMs();
    }

    stats.totalMs = total.elapsedMs();
    return Result::success();
}

void Pipeline::releaseBuffers() noexcept {
    encodedFull_.release();
    inferenceImg_.release();
    rawMatte_.release();
    fullMatte_.release();
}

} // namespace aibgr

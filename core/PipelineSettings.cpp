// SPDX-License-Identifier: MIT
#include <cstdint>
#include "core/PipelineSettings.h"

#include "util/Hash.h"

#include <algorithm>
#include <cmath>

namespace aibgr {

int qualityResolution(QualityPreset q) noexcept {
    switch (q) {
    case QualityPreset::Draft256: return 256;
    case QualityPreset::Fast384: return 384;
    case QualityPreset::Balanced512: return 512;
    case QualityPreset::High768: return 768;
    case QualityPreset::Maximum1024: return 1024;
    }
    return 512;
}

const char* qualityName(QualityPreset q) noexcept {
    switch (q) {
    case QualityPreset::Draft256: return "Draft (256)";
    case QualityPreset::Fast384: return "Fast (384)";
    case QualityPreset::Balanced512: return "Balanced (512)";
    case QualityPreset::High768: return "High (768)";
    case QualityPreset::Maximum1024: return "Maximum (1024)";
    }
    return "?";
}

const char* gpuBackendName(GpuBackend b) noexcept {
    switch (b) {
    case GpuBackend::Auto: return "Auto";
    case GpuBackend::Cuda: return "CUDA";
    case GpuBackend::DirectMl: return "DirectML";
    case GpuBackend::Cpu: return "CPU";
    case GpuBackend::Disabled: return "Disabled";
    }
    return "?";
}

const char* promptRecomputeName(PromptRecompute p) noexcept {
    switch (p) {
    case PromptRecompute::EveryFrame: return "Every frame";
    case PromptRecompute::WhilePlaying: return "Reuse while playing";
    case PromptRecompute::OncePerPromptChange: return "Once per prompt change";
    }
    return "?";
}

const char* modelIdName(ModelId m) noexcept {
    switch (m) {
    case ModelId::None: return "None (fallback key)";
    case ModelId::Auto: return "Auto";
    case ModelId::BirefNetLite: return "BiRefNet Lite";
    case ModelId::BirefNetGeneral: return "BiRefNet General";
    case ModelId::BirefNetPortrait: return "BiRefNet Portrait";
    case ModelId::SmallMatte: return "U2NetP CPU";
    case ModelId::ViTMatte: return "ViTMatte (detail refine)";
    }
    return "?";
}

namespace {
template <typename T>
inline void clampTo(T& v, T lo, T hi) noexcept {
    v = std::min(hi, std::max(lo, v));
}
} // namespace

void PipelineSettings::sanitise() noexcept {
    // Non-finite values are replaced FIRST, before any clamping. std::min/std::max fold a NaN into
    // whichever bound they compare against, so clamping first would silently turn a NaN into 0 and
    // the fallback below would never fire - the exact case it exists for.
    if (!std::isfinite(detailRefinement)) detailRefinement = 0.0f;
    if (!std::isfinite(edgeFeather)) edgeFeather = 0.0f;
    if (!std::isfinite(edgeExpansion)) edgeExpansion = 0.0f;
    if (!std::isfinite(edgeSmoothing)) edgeSmoothing = 0.0f;
    if (!std::isfinite(shiftMatte)) shiftMatte = 0.0f;
    // Falls back to the DEFAULT (1.0 = remove the background), not to 0.0: a NaN from a broken
    // keyframe should still do the plugin's job rather than silently disabling it.
    if (!std::isfinite(backgroundTransparency)) backgroundTransparency = 1.0f;
    if (!std::isfinite(fallbackKeyTolerance)) fallbackKeyTolerance = 0.25f;
    if (!std::isfinite(fallbackKeySoftness)) fallbackKeySoftness = 0.08f;
    if (!std::isfinite(temporalStrength)) temporalStrength = 0.0f;
    if (!std::isfinite(motionSensitivity)) motionSensitivity = 0.5f;

    despecklePx = std::max(0, std::min(1000, despecklePx));

    clampTo(detailRefinement, 0.0f, 100.0f);
    clampTo(edgeFeather, 0.0f, 100.0f);
    clampTo(edgeExpansion, -100.0f, 100.0f);
    clampTo(edgeSmoothing, 0.0f, 100.0f);
    clampTo(shiftMatte, -0.5f, 0.5f);

    clampTo(backgroundTransparency, 0.0f, 1.0f);
    clampTo(fallbackKeyTolerance, 0.0f, 1.0f);
    // A zero softness would make the smoothstep band degenerate and the key a hard silhouette.
    clampTo(fallbackKeySoftness, 0.0001f, 1.0f);

    gpuDeviceId = std::max(0, std::min(7, gpuDeviceId));
    gpuMemoryLimitMB = std::max(0, std::min(65536, gpuMemoryLimitMB));
    maxConcurrentInference = std::max(1, std::min(4, maxConcurrentInference));
    cacheSizeMB = std::max(0, std::min(8192, cacheSizeMB));
    intraOpThreads = std::max(0, std::min(64, intraOpThreads));

    clampTo(temporalStrength, 0.0f, 1.0f);
    clampTo(motionSensitivity, 0.0f, 1.0f);

    switch (promptRecompute) {
    case PromptRecompute::EveryFrame:
    case PromptRecompute::WhilePlaying:
    case PromptRecompute::OncePerPromptChange: break;
    default: promptRecompute = PromptRecompute::EveryFrame; break;
    }
}

uint64_t PipelineSettings::rawMatteHash() const noexcept {
    // Everything that changes what the segmentation stage itself produces. Deliberately excludes
    // the refinement controls, so scrubbing a feather slider stays a cache hit.
    return HashBuilder()
        .boolean(enable)
        .u64(static_cast<uint64_t>(processingMode))
        .u64(static_cast<uint64_t>(quality))
        .u64(static_cast<uint64_t>(model))
        .u64(static_cast<uint64_t>(inputColorSpace))
        .u64(static_cast<uint64_t>(processingResolution))
        .i32(despecklePx)
        .flt(fallbackKeyTolerance)
        .flt(fallbackKeySoftness)
        .value();
}

uint64_t PipelineSettings::refinementHash() const noexcept {
    // Everything that changes the refined matte, and therefore invalidates temporal state.
    return HashBuilder(rawMatteHash())
        .flt(detailRefinement)
        .flt(edgeFeather)
        .flt(edgeExpansion)
        .flt(edgeSmoothing)
        .flt(shiftMatte)
        .flt(temporalStrength)
        .flt(motionSensitivity)
        .value();
}

} // namespace aibgr

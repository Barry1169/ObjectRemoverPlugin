// SPDX-License-Identifier: MIT
#pragma once

#include "core/ColorSpace.h"
#include "core/Premultiply.h"
#include "core/Types.h"

#include <cstdint>

namespace aibgr {

/// Mirrors the `outputMode` inspector choice. See docs/02-architecture.md section 8, tab 3.
enum class OutputMode : uint8_t {
    Composite = 0,    ///< original subject over transparency
    ForegroundOnly,   ///< subject premultiplied, background zeroed
    BackgroundOnly,   ///< the removed background, subject hole governed by backgroundTransparency
    MatteOnly,        ///< the alpha as a greyscale image
};

/// Mirrors `processingMode`.
enum class ProcessingMode : uint8_t {
    Automatic = 0,
    LargestSubjectOnly,
    AllSubjects,
    InvertRemoveSubject,
};

/// Mirrors `quality` (Segmentation Quality). The number is the model's working resolution on the
/// long edge.
enum class QualityPreset : uint8_t {
    Draft256 = 0,
    Fast384,
    Balanced512,
    High768,
    Maximum1024,
};

int qualityResolution(QualityPreset q) noexcept;
const char* qualityName(QualityPreset q) noexcept;

/// Mirrors `aiPromptRecompute`: how often a prompt-based selector may re-run the model.
///
/// A still image is a cache hit on every frame, so it is cheap regardless. Video is not: every
/// frame is new pixels, and one CPU encoder pass costs on the order of a second. This policy is the
/// honest choice between accuracy and playback speed.
enum class PromptRecompute : uint8_t {
    EveryFrame = 0,        ///< follow motion exactly, at per-frame inference cost
    WhilePlaying,          ///< reuse the last matte during interactive playback only
    OncePerPromptChange,   ///< never follow motion; recompute only when prompts/settings change
};

const char* promptRecomputeName(PromptRecompute p) noexcept;

/// True when a selector may return its previous matte instead of recomputing. `interacting` is the
/// host's interactive/draft flag for this render; it only matters for WhilePlaying.
inline bool reusePromptResult(PromptRecompute policy, bool interacting) noexcept {
    switch (policy) {
    case PromptRecompute::WhilePlaying: return interacting;
    case PromptRecompute::OncePerPromptChange: return true;
    case PromptRecompute::EveryFrame: break;
    }
    return false;
}

/// Mirrors `processingResolution`.
enum class ProcessingResolutionMode : uint8_t {
    FollowHost = 0,
    ForceHalf,
    ForceQuarter,
    ForceFull,
};

/// Mirrors `gpuAcceleration`.
enum class GpuBackend : uint8_t {
    Auto = 0,
    Cuda,
    DirectMl,
    Cpu,
    Disabled,
};

const char* gpuBackendName(GpuBackend b) noexcept;

/// Mirrors `model`. Populated at describe time from the models actually found on disk.
enum class ModelId : uint8_t {
    None = 0,   ///< no model: fall back to the built-in colour key
    Auto,       ///< best available
    BirefNetLite,
    BirefNetGeneral,
    BirefNetPortrait,
    SmallMatte,
    ViTMatte,
};

const char* modelIdName(ModelId m) noexcept;

/// Every inspector control, as a plain value type.
///
/// This struct is the ONLY thing that crosses the boundary between the OFX plugin layer and the
/// host-agnostic pipeline, which is what makes the pipeline unit-testable without a host.
struct PipelineSettings {
    // --- Background Removal ---
    bool enable = true;
    ProcessingMode processingMode = ProcessingMode::Automatic;
    QualityPreset quality = QualityPreset::Balanced512;
    ModelId model = ModelId::Auto;
    ColorSpaceAssumption inputColorSpace = ColorSpaceAssumption::Auto;
    int despecklePx = 8;

    // --- Matte Refinement (ranges as exposed in the inspector) ---
    float detailRefinement = 60.0f; // 0..100
    float edgeFeather = 1.0f;       // 0..100 px
    float edgeExpansion = 0.0f;     // -100..100 px
    float edgeSmoothing = 20.0f;    // 0..100
    float shiftMatte = 0.0f;        // -0.5..0.5

    // --- Output ---
    OutputMode outputMode = OutputMode::Composite;
    /// How transparent the background becomes. 1.0 (the default) removes it completely, which is
    /// the whole point of the plugin; 0.0 leaves the frame untouched and makes the effect an
    /// identity. See composite/Compositor.h.
    float backgroundTransparency = 1.0f; // 0..1
    bool matteInvert = false;
    Premultiplication outputPremultiplication = Premultiplication::Unknown; // Unknown = follow host

    // --- AI prompts (box/point selection) ---
    PromptRecompute promptRecompute = PromptRecompute::EveryFrame;

    // --- Performance ---
    ProcessingResolutionMode processingResolution = ProcessingResolutionMode::FollowHost;
    GpuBackend gpuBackend = GpuBackend::Auto;
    int gpuDeviceId = 0;
    int gpuMemoryLimitMB = 2048;
    int maxConcurrentInference = 1;
    bool enableMatteCache = true;
    int cacheSizeMB = 512;
    int intraOpThreads = 0;

    // --- Temporal (Video) ---
    float temporalStrength = 0.35f;  // 0..1
    float motionSensitivity = 0.5f;  // 0..1

    // --- Diagnostics / fallback ---
    /// Tolerance of the built-in border-colour key used when no AI model is available. Exposed
    /// under Diagnostics so the plugin is still useful with no model installed, rather than
    /// merely passing through.
    float fallbackKeyTolerance = 0.25f; // 0..1
    float fallbackKeySoftness = 0.08f;  // 0..1

    /// Long edge of the matte the segmentation stage should produce.
    int matteResolution() const noexcept { return qualityResolution(quality); }

    /// Clamp every value into its documented range. Called once per render so a malformed
    /// animated keyframe or a hand-edited project cannot push the maths out of domain.
    void sanitise() noexcept;

    /// Hash of the subset that changes the RAW segmented matte (cache key).
    uint64_t rawMatteHash() const noexcept;
    /// Hash of the subset that changes the REFINED matte (temporal-state validity).
    uint64_t refinementHash() const noexcept;
};

} // namespace aibgr

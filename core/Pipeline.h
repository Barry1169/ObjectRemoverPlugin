// SPDX-License-Identifier: MIT
#pragma once

#include "core/ColorSpace.h"
#include "core/FloatImage.h"
#include "core/FrameContext.h"
#include "core/PipelineSettings.h"
#include "matte/Matte.h"
#include "matte/PlaceholderMatte.h"
#include "util/Status.h"

#include <memory>
#include <string>

namespace aibgr {

/// Abstract producer of a raw alpha matte.
///
/// Phase 2 ships exactly one implementation, the fallback colour key. Phase 4 adds the ONNX
/// Runtime segmenters behind the same interface, at which point nothing else in the pipeline
/// changes. That is the whole reason the interface exists.
class MatteSource {
public:
    virtual ~MatteSource() = default;

    /// Fill `out` with a matte the same size as `image`.
    /// `image` is sRGB-encoded, un-premultiplied RGBA float32.
    /// Must not throw.
    virtual Result acquire(const FloatImage& image, Matte& out) = 0;

    /// Short human-readable name, for the Diagnostics status line.
    virtual const char* name() const noexcept = 0;
};

/// Optional full-resolution manual correction, applied after the base matte is upscaled.
class MatteOverride {
public:
    virtual ~MatteOverride() = default;
    virtual Result apply(Matte& matte, bool inverted = false) const = 0;
};

/// The built-in border-colour key, wrapped as a MatteSource.
class FallbackKeyMatteSource final : public MatteSource {
public:
    explicit FallbackKeyMatteSource(FallbackKeySettings settings = {}) : settings_(settings) {}
    Result acquire(const FloatImage& image, Matte& out) override;
    const char* name() const noexcept override { return "FallbackKey"; }
    void setSettings(const FallbackKeySettings& s) noexcept { settings_ = s; }
    const FallbackKeySettings& settings() const noexcept { return settings_; }

private:
    FallbackKeySettings settings_;
};

/// Host-supplied abort polling. The plugin layer implements this over the OFX abort callback so
/// the pipeline can bail out of a long render without knowing anything about OFX.
class AbortChecker {
public:
    virtual ~AbortChecker() = default;
    virtual bool aborted() const noexcept = 0;
};

struct PipelineStats {
    bool matteSourceRan = false;
    bool usedFallbackKey = false;
    bool colourTransformed = false;
    bool matteUpscaled = false;
    ColorSpaceAssumption appliedColorSpace = ColorSpaceAssumption::DisplayReferred;
    int matteWidth = 0;
    int matteHeight = 0;
    double matteSourceMs = 0.0;
    double refineMs = 0.0;
    double compositeMs = 0.0;
    double totalMs = 0.0;
    std::string message;
};

/// The per-frame orchestrator. Host-agnostic: no OFX types, no ONNX Runtime.
///
/// Buffer policy: every scratch buffer is a member and is grow-only, so after the first frame of a
/// sequence the steady state performs no heap allocation at all (audited in Phase 7).
class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Non-owning. Pass nullptr to use the built-in fallback key.
    void setMatteSource(MatteSource* source) noexcept { matteSource_ = source; }
    MatteSource* matteSource() const noexcept { return matteSource_; }
    void setMatteOverride(const MatteOverride* value) noexcept { matteOverride_ = value; }

    void setAbortChecker(const AbortChecker* checker) noexcept { abortChecker_ = checker; }

    /// `srcStraight` is un-premultiplied RGBA float32 in the host's colour encoding.
    /// `dst` is written un-premultiplied, same encoding, same dimensions.
    /// Returns Status::Cancelled if the host aborted mid-render.
    Result process(const FrameContext& ctx, const PipelineSettings& settings,
                   const FloatImage& srcStraight, FloatImage& dst, PipelineStats& stats);

    /// Release every scratch buffer. Bound to the inspector's "Clear Cache" button.
    void releaseBuffers() noexcept;

    /// Long edge of the matte this pipeline would produce, given the settings and frame size.
    /// Exposed so the plugin can report it in Diagnostics without running a frame.
    static int resolveMatteLongEdge(const PipelineSettings& settings, int frameWidth,
                                    int frameHeight) noexcept;

private:
    bool aborted() const noexcept;

    /// Compute the matte resolution to actually use, honouring quality + processingResolution.
    static void computeMatteSize(const PipelineSettings& settings, int srcW, int srcH, int& outW,
                                 int& outH) noexcept;

    MatteSource* matteSource_ = nullptr;
    const MatteOverride* matteOverride_ = nullptr;
    std::unique_ptr<FallbackKeyMatteSource> fallback_;
    const AbortChecker* abortChecker_ = nullptr;

    // Grow-only scratch. Named for what they hold, not for their size.
    FloatImage encodedFull_;   // sRGB-encoded copy, only when a colour transform is needed
    FloatImage inferenceImg_;  // downscaled image actually handed to the matte source
    Matte rawMatte_;           // matte source output, at matte resolution
    Matte fullMatte_;          // refined matte, upscaled to frame resolution
};

} // namespace aibgr

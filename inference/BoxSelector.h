// SPDX-License-Identifier: MIT
#pragma once
#include "guidance/BrushStrokes.h"
#include <memory>
namespace aibgr {
/// Experimental prompt-based selection. One instance is used under the plugin's render mutex.
class BoxSelector final : public MatteRefiner {
public:
    BoxSelector(); ~BoxSelector();
    /// `intraOpThreads` 0 = automatic (all cores, capped). The value is baked into the ONNX
    /// sessions, so changing it requires a new open().
    Result open(const std::string& runtime, const std::string& encoder, const std::string& decoder,
                int intraOpThreads = 0);
    /// `reuseCached` lets the selector return its previous matte for a NEW frame that has the same
    /// prompts and geometry. That is the difference between smooth playback and one model pass per
    /// frame; see PipelineSettings::PromptRecompute.
    void configure(std::vector<BrushStroke> strokes, FrameContext ctx, bool recoverDetail = false,
                   bool reuseCached = false);
    Result refine(const FloatImage& image, Matte& matte) override;
    void clearCache() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<BrushStroke> strokes_;
    FrameContext ctx_;
    bool recoverDetail_ = false;
    bool reuseCached_ = false;
};
} // namespace aibgr

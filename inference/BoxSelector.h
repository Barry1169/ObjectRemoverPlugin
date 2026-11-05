// SPDX-License-Identifier: MIT
#pragma once
#include "guidance/BrushStrokes.h"
#include <memory>
namespace aibgr {
/// Experimental prompt-based selection. One instance is used under the plugin's render mutex.
class BoxSelector final : public MatteRefiner {
public:
    BoxSelector(); ~BoxSelector();
    Result open(const std::string& runtime, const std::string& encoder, const std::string& decoder);
    /// Load the optional non-commercial BRIA model after EfficientSAM is open.
    Result openBria(const std::string& model);
    void configure(std::vector<BrushStroke> strokes, FrameContext ctx, bool recoverDetail = false, bool bria = false);
    Result refine(const FloatImage& image, Matte& matte, bool inverted) override;
    void clearCache() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::vector<BrushStroke> strokes_;
    FrameContext ctx_;
    bool recoverDetail_ = false;
    bool bria_ = false;
};
} // namespace aibgr

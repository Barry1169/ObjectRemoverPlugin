// SPDX-License-Identifier: MIT
#pragma once
#include "guidance/BrushStrokes.h"
#include <utility>
namespace aibgr {
/// Seeded, edge-aware colour selection (not GIMP/GEGL code). A bounded CPU approximation.
class GuidedSelection final : public MatteRefiner {
public:
    void configure(std::vector<BrushStroke> strokes, FrameContext ctx) { strokes_ = std::move(strokes); ctx_ = ctx; }
    Result refine(const FloatImage& image, Matte& matte, bool inverted) override;
    void releaseBuffers() noexcept;
private:
    std::vector<BrushStroke> strokes_;
    FrameContext ctx_;
    FloatImage smallImage_;
    Matte seedFull_, seedSmall_, priorSmall_, resultSmall_, resultFull_;
};
}

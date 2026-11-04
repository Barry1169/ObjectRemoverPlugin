// SPDX-License-Identifier: MIT
#pragma once
#include "core/Pipeline.h"
#include "core/FrameContext.h"
#include <string>
#include <vector>

namespace aibgr {
struct BrushPoint { double x = 0, y = 0; };
struct BrushStroke { bool keep = true; double radius = 20; std::vector<BrushPoint> points; bool outline = false; };
// Hard limits keep host-supplied project data and render work bounded.
constexpr size_t kMaxBrushPoints = 8192;
constexpr size_t kMaxPointsPerStroke = 4096;
constexpr size_t kLegacyBrushPoints = 512;
/// Simplify a newly drawn polyline without moving its points more than tolerance.
void simplifyBrushStroke(BrushStroke& stroke, double tolerance);
/// Persisted data uses canonical OFX coordinates so proxies and non-zero image origins work.
bool parseBrushStrokes(const std::string& text, std::vector<BrushStroke>& result);
std::string serialiseBrushStrokes(const std::vector<BrushStroke>& strokes);
class BrushCorrections final : public MatteOverride {
public:
    void configure(std::vector<BrushStroke> strokes, FrameContext context) {
        strokes_ = std::move(strokes); context_ = context;
    }
    Result apply(Matte& matte, bool inverted = false) const override;
private:
    std::vector<BrushStroke> strokes_;
    FrameContext context_;
};
} // namespace aibgr

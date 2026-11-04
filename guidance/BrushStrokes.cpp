// SPDX-License-Identifier: MIT
#include "guidance/BrushStrokes.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace aibgr {
bool parseBrushStrokes(const std::string& text, std::vector<BrushStroke>& out) {
    out.clear();
    if (text.empty()) return true;
    if (text.size() > 200000) return false;
    std::istringstream in(text); in.imbue(std::locale::classic());
    std::string version; size_t count = 0;
    if (!(in >> version >> count) || version != "AIBGR1" || count > kMaxBrushPoints) return false;
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        char kind = 0; double radius = 0; size_t n = 0;
        if (!(in >> kind >> radius >> n) || (kind != 'K' && kind != 'R') || !std::isfinite(radius) ||
            radius <= 0 || radius > 200 || n == 0 || n > kMaxBrushPoints - total) { out.clear(); return false; }
        BrushStroke stroke; stroke.keep = kind == 'K'; stroke.radius = radius;
        for (size_t j = 0; j < n; ++j) {
            BrushPoint p;
            if (!(in >> p.x >> p.y) || !std::isfinite(p.x) || !std::isfinite(p.y) ||
                std::abs(p.x) > 1000000 || std::abs(p.y) > 1000000) { out.clear(); return false; }
            stroke.points.push_back(p);
        }
        total += n; out.push_back(std::move(stroke));
    }
    in >> std::ws;
    if (!in.eof()) { out.clear(); return false; }
    return true;
}
std::string serialiseBrushStrokes(const std::vector<BrushStroke>& strokes) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "AIBGR1 " << strokes.size() << ' ' << std::fixed << std::setprecision(3);
    for (const auto& s : strokes) {
        out << (s.keep ? 'K' : 'R') << ' ' << s.radius << ' ' << s.points.size() << ' ';
        for (const auto& p : s.points) out << p.x << ' ' << p.y << ' ';
    }
    return out.str();
}
Result BrushCorrections::apply(Matte& matte, bool inverted) const {
    if (matte.empty()) return Result::success();
    const double sx = context_.renderScaleX / std::max(context_.pixelAspectX, 1e-8);
    const double sy = context_.renderScaleY;
    if (!(sx > 0 && sy > 0 && sx <= 100 && sy <= 100 && std::isfinite(sx) && std::isfinite(sy))) return Result::fail(Status::InvalidArgument, "invalid brush render scale");
    // Draw each stroke in order, as soft capsules. Coordinates are evaluated in canonical
    // space, so a round brush remains correct for proxy scales and non-square pixels.
    for (const auto& s : strokes_) {
        if (s.points.empty()) continue;
        for (size_t i = 0; i < s.points.size(); ++i) {
            const auto a = s.points[i == 0 ? 0 : i - 1], b = s.points[i];
            auto low = [](double v, int size) {
                return v <= 0 ? 0 : v >= size ? size : static_cast<int>(std::floor(v));
            };
            auto high = [](double v, int size) {
                return v < 0 ? -1 : v >= size - 1 ? size - 1 : static_cast<int>(std::ceil(v));
            };
            const int x0 = low((std::min(a.x,b.x)-s.radius)*sx-context_.renderWindow.x1, matte.width());
            const int x1 = high((std::max(a.x,b.x)+s.radius)*sx-context_.renderWindow.x1, matte.width());
            const int y0 = low((std::min(a.y,b.y)-s.radius)*sy-context_.renderWindow.y1, matte.height());
            const int y1 = high((std::max(a.y,b.y)+s.radius)*sy-context_.renderWindow.y1, matte.height());
            const double dx=b.x-a.x, dy=b.y-a.y, len=dx*dx+dy*dy;
            for (int y=y0; y<=y1; ++y) for (int x=x0; x<=x1; ++x) {
                const double cx = (x + context_.renderWindow.x1 + 0.5) / sx;
                const double cy = (y + context_.renderWindow.y1 + 0.5) / sy;
                const double t = len > 0 ? std::clamp(((cx-a.x)*dx+(cy-a.y)*dy)/len, 0.0, 1.0) : 0.0;
                const double dist = std::hypot(cx-(a.x+t*dx), cy-(a.y+t*dy));
                // Feather the outer 10 percent to avoid jagged edges.
                const float strength = static_cast<float>(std::clamp((s.radius-dist)/(s.radius*0.1), 0.0, 1.0));
                float& v = matte.at(x,y);
                const bool target = s.keep != inverted;
                v = v * (1.0f-strength) + (target ? strength : 0.0f);
            }
        }
    }
    return Result::success();
}
} // namespace aibgr

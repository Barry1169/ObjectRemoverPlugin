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
    if (text.size() > 400000) return false;
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string version;
    size_t count = 0;
    if (!(in >> version >> count) ||
        (version != "AIBGR1" && version != "AIBGR2" && version != "AIBGR3" &&
         version != "AIBGR4" && version != "AIBGR5") ||
        count > kMaxBrushPoints) {
        return false;
    }
    // AIBGR5 adds single-point AI prompts ('P' keep, 'N' remove). Older builds reject the whole
    // string rather than silently dropping the unsupported prompts, which is the safe failure.
    const bool pointsAllowed = version == "AIBGR5";
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        char kind = 0;
        double radius = 0;
        size_t n = 0;
        if (!(in >> kind >> radius >> n)) { out.clear(); return false; }
        const bool boxKind = kind == 'A' || kind == 'D';
        const bool pointKind = kind == 'P' || kind == 'N';
        const bool lineKind = kind == 'L' && version != "AIBGR1";
        const bool brushKind = kind == 'K' || kind == 'R';
        if (!(boxKind || brushKind || lineKind || (pointKind && pointsAllowed)) ||
            !std::isfinite(radius) || radius <= 0 || radius > 200 || n == 0 ||
            (boxKind && n != 2) || (pointKind && n != 1) || n > kMaxPointsPerStroke ||
            n > kMaxBrushPoints - total) {
            out.clear();
            return false;
        }
        BrushStroke stroke;
        stroke.keep = kind == 'K' || kind == 'A' || kind == 'P';
        stroke.outline = lineKind;
        stroke.box = boxKind;
        stroke.point = pointKind;
        stroke.radius = radius;
        for (size_t j = 0; j < n; ++j) {
            BrushPoint p;
            if (!(in >> p.x >> p.y) || !std::isfinite(p.x) || !std::isfinite(p.y) ||
                std::abs(p.x) > 1000000 || std::abs(p.y) > 1000000) {
                out.clear();
                return false;
            }
            stroke.points.push_back(p);
        }
        total += n;
        out.push_back(std::move(stroke));
    }
    in >> std::ws;
    if (!in.eof()) { out.clear(); return false; }
    return true;
}

std::string serialiseBrushStrokes(const std::vector<BrushStroke>& strokes) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const bool outlines =
        std::any_of(strokes.begin(), strokes.end(), [](const BrushStroke& s) { return s.outline; });
    size_t total = 0;
    bool boxes = false, points = false;
    for (const auto& s : strokes) {
        total += s.points.size();
        boxes = boxes || s.box;
        points = points || s.point;
    }
    // Stay on the most widely readable version that can express the data: point prompts are the
    // only thing that needs AIBGR5, and boxes alone stay AIBGR4.
    const char* version = points ? "AIBGR5"
                                 : boxes ? "AIBGR4"
                                         : total > kLegacyBrushPoints ? "AIBGR3"
                                                                      : outlines ? "AIBGR2"
                                                                                 : "AIBGR1";
    out << version << ' ' << strokes.size() << ' ' << std::fixed << std::setprecision(3);
    for (const auto& s : strokes) {
        const char kind = s.box ? (s.keep ? 'A' : 'D')
                                : s.point ? (s.keep ? 'P' : 'N')
                                          : s.outline ? 'L' : s.keep ? 'K' : 'R';
        out << kind << ' ' << s.radius << ' ' << s.points.size() << ' ';
        for (const auto& p : s.points) out << p.x << ' ' << p.y << ' ';
    }
    return out.str();
}

void simplifyBrushStroke(BrushStroke& stroke, double tolerance) {
    auto& points=stroke.points;
    if (points.size()<3 || !std::isfinite(tolerance) || tolerance<=0) return;
    // Iterative Ramer-Douglas-Peucker; recursion is avoided for hostile/complex input.
    std::vector<unsigned char> keep(points.size(),0); keep.front()=keep.back()=1;
    std::vector<std::pair<size_t,size_t>> ranges{{0,points.size()-1}};
    const double limit=tolerance*tolerance;
    while(!ranges.empty()) {
        const auto [first,last]=ranges.back(); ranges.pop_back();
        const auto a=points[first], b=points[last];
        const double dx=b.x-a.x,dy=b.y-a.y,len=dx*dx+dy*dy;
        double farthest=limit; size_t index=0;
        for(size_t i=first+1;i<last;++i) {
            const double t=len>0 ? std::clamp(((points[i].x-a.x)*dx+(points[i].y-a.y)*dy)/len,0.0,1.0) : 0.0;
            const double x=points[i].x-(a.x+t*dx), y=points[i].y-(a.y+t*dy), d=x*x+y*y;
            if(d>farthest) { farthest=d; index=i; }
        }
        if(index) { keep[index]=1; ranges.emplace_back(first,index); ranges.emplace_back(index,last); }
    }
    size_t dest=0; for(size_t i=0;i<points.size();++i) if(keep[i]) points[dest++]=points[i];
    points.resize(dest);
}
Result BrushCorrections::apply(Matte& matte, bool inverted) const {
    if (matte.empty()) return Result::success();
    const double sx = context_.renderScaleX / std::max(context_.pixelAspectX, 1e-8);
    const double sy = context_.renderScaleY;
    if (!(sx > 0 && sy > 0 && sx <= 100 && sy <= 100 && std::isfinite(sx) && std::isfinite(sy))) return Result::fail(Status::InvalidArgument, "invalid brush render scale");
    // Draw each stroke in order, as soft capsules. Coordinates are evaluated in canonical
    // space, so a round brush remains correct for proxy scales and non-square pixels.
    for (const auto& s : strokes_) {
        if (isAiPrompt(s) || s.outline || s.points.empty()) continue;
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

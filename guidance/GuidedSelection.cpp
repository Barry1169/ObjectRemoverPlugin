// SPDX-License-Identifier: MIT
#include "guidance/GuidedSelection.h"
#include "matte/Resize.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace aibgr {
namespace {
constexpr int kMaxEdge = 320;
constexpr int kBins = 8;
float channel(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }
int bin(float v) { return std::min(kBins - 1, static_cast<int>(channel(v) * kBins)); }
int idx(int r,int g,int b) { return (r*kBins+g)*kBins+b; }
float similarity(const float* a, const float* b) {
    float d=0; for(int c=0;c<3;++c) { const float v=channel(a[c])-channel(b[c]); d+=v*v; }
    return 0.02f + std::exp(-d * 20.0f);
}
}
void GuidedSelection::releaseBuffers() noexcept {
    smallImage_.release(); seedFull_.release(); seedSmall_.release(); priorSmall_.release();
    resultSmall_.release(); resultFull_.release();
}
Result GuidedSelection::refine(const FloatImage& image, Matte& matte, bool inverted) {
    try {
        if (image.empty() || matte.width()!=image.width() || matte.height()!=image.height())
            return Result::fail(Status::InvalidArgument, "image/matte dimensions differ");
        if (strokes_.empty()) return Result::success();
        const double scale=std::min(1.0, double(kMaxEdge)/std::max(image.width(),image.height()));
        const int w=std::max(1,int(std::round(image.width()*scale)));
        const int h=std::max(1,int(std::round(image.height()*scale)));
        smallImage_.resize(w,h); resizeArea(image,smallImage_);
        seedFull_.resize(image.width(),image.height()); seedFull_.fill(0.5f);
        BrushCorrections brush; brush.configure(strokes_,ctx_);
        Result painted=brush.apply(seedFull_); if (!painted.ok()) return painted;
        seedSmall_.resize(w,h); resizeArea(seedFull_,seedSmall_);
        priorSmall_.resize(w,h); resizeArea(matte,priorSmall_);
        // Latest closed outline marks everything outside as background. Painted strokes override it.
        const BrushStroke* outline=nullptr;
        for (const auto& s: strokes_) if (!s.box && s.outline && s.points.size()>=3) outline=&s;
        const double sx=ctx_.renderScaleX/std::max(ctx_.pixelAspectX,1e-8), sy=ctx_.renderScaleY;
        if (!(sx>0 && sy>0 && std::isfinite(sx) && std::isfinite(sy))) return Result::fail(Status::InvalidArgument,"invalid coordinates");
        const size_t n=static_cast<size_t>(w)*h;
        std::vector<unsigned char> seed(n, 0); // 0 unknown, 1 keep, 2 remove
        std::array<float,512> fg{}, bg{}; size_t nf=0, nb=0;
        std::vector<double> crossings;
        for(int y=0;y<h;++y) {
          // Intersect the outline once per scanline instead of testing every edge for each pixel.
          crossings.clear();
          if (outline) {
            const double cy=((y+0.5)*image.height()/h+ctx_.renderWindow.y1)/sy;
            const auto& points=outline->points;
            for(size_t i=0,j=points.size()-1; i<points.size(); j=i++) {
              const auto a=points[i], b=points[j];
              if ((a.y>cy)!=(b.y>cy)) crossings.push_back((b.x-a.x)*(cy-a.y)/(b.y-a.y)+a.x);
            }
            std::sort(crossings.begin(),crossings.end());
          }
          for(int x=0;x<w;++x) {
            const size_t i=static_cast<size_t>(y)*w+x;
            const float mark=seedSmall_.at(x,y);
            // Preserve small strokes when full-resolution seeds are area-downsampled. A narrow
            // line covering 4% of a cell should still be a useful sample, not disappear.
            if (mark>0.52f) seed[i]=1;
            else if (mark<0.48f) seed[i]=2;
            else if (outline) {
                const double cx=((x+0.5)*image.width()/w+ctx_.renderWindow.x1)/sx;
                const auto after=std::upper_bound(crossings.begin(),crossings.end(),cx);
                if ((crossings.end()-after)%2 == 0) seed[i]=2;
            }
            const float* p=smallImage_.pixel(x,y); const int b=idx(bin(p[0]),bin(p[1]),bin(p[2]));
            if (seed[i]==1) { fg[b]++; ++nf; }
            if (seed[i]==2) { bg[b]++; ++nb; }
          }
        }
        // Without both classes, the current AI/key matte remains the baseline.
        if (!nf || !nb) return Result::success();
        // Smooth coarse colour histograms, making samples useful beyond the exact marked pixels.
        std::array<float,512> fgs{}, bgs{};
        for(int r=0;r<kBins;++r) for(int g=0;g<kBins;++g) for(int b=0;b<kBins;++b) {
            float f=0, v=0;
            for(int dr=-1;dr<=1;++dr) for(int dg=-1;dg<=1;++dg) for(int db=-1;db<=1;++db) {
                const int rr=r+dr,gg=g+dg,bb=b+db;
                if(rr<0||rr>=kBins||gg<0||gg>=kBins||bb<0||bb>=kBins) continue;
                const float wt=1.0f/(1.0f+dr*dr+dg*dg+db*db);
                f += wt*fg[idx(rr,gg,bb)]/static_cast<float>(nf);
                v += wt*bg[idx(rr,gg,bb)]/static_cast<float>(nb);
            }
            fgs[idx(r,g,b)]=f; bgs[idx(r,g,b)]=v;
        }
        std::vector<float> unary(n), curr(n), next(n), right(n), up(n);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const size_t i=static_cast<size_t>(y)*w+x; const float* p=smallImage_.pixel(x,y);
            const int b=idx(bin(p[0]),bin(p[1]),bin(p[2]));
            const float total=fgs[b]+bgs[b], prior=inverted ? 1.0f-priorSmall_.at(x,y) : priorSmall_.at(x,y);
            // Unknown colours retain AI's opinion; sampled colours influence the uncertain area.
            const float confidence=total/(total+0.001f);
            const float colour=total>0 ? fgs[b]/total : prior;
            unary[i]=std::clamp(prior*(1.0f-0.8f*confidence)+colour*(0.8f*confidence),0.0f,1.0f);
            curr[i]=seed[i] ? (seed[i]==1 ? 1.0f : 0.0f) : unary[i];
            if (x+1<w) right[i]=similarity(p,smallImage_.pixel(x+1,y));
            if (y+1<h) up[i]=similarity(p,smallImage_.pixel(x,y+1));
        }
        for (int iteration=0;iteration<40;++iteration) {
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                const size_t i=static_cast<size_t>(y)*w+x;
                if(seed[i]) { next[i]=seed[i]==1 ? 1.0f : 0.0f; continue; }
                float sum=0.6f*unary[i], weight=0.6f;
                auto add=[&](size_t j,float wt) { sum+=curr[j]*wt; weight+=wt; };
                if(x>0) add(i-1,right[i-1]);
                if(x+1<w) add(i+1,right[i]);
                if(y>0) add(i-w,up[i-w]);
                if(y+1<h) add(i+w,up[i]);
                next[i]=sum/weight;
            }
            curr.swap(next);
        }
        resultSmall_.resize(w,h);
        for(size_t i=0;i<n;++i) resultSmall_.data()[i]=inverted ? 1.0f-curr[i] : curr[i];
        resultFull_.resize(image.width(),image.height()); resizeEdgeAware(resultSmall_,smallImage_,image,resultFull_);
        std::copy(resultFull_.data(),resultFull_.data()+resultFull_.floatCount(),matte.data());
        return Result::success();
    } catch (const std::exception& e) { return Result::fail(Status::BackendError,e.what()); }
}
} // namespace aibgr

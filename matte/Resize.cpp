// SPDX-License-Identifier: MIT
#include "matte/Resize.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace aibgr {

namespace {

/// Catmull-Rom weights for a fractional position t in [0,1), for the four taps at -1, 0, 1, 2.
inline void catmullRomWeights(float t, float w[4]) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    w[0] = -0.5f * t3 + t2 - 0.5f * t;
    w[1] = 1.5f * t3 - 2.5f * t2 + 1.0f;
    w[2] = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    w[3] = 0.5f * t3 - 0.5f * t2;
}

inline int clampIndex(int i, int n) noexcept { return std::min(n - 1, std::max(0, i)); }

/// One-dimensional resample of a row of floats.
void resampleRow1D(const float* src, int srcN, float* dst, int dstN) {
    if (srcN <= 0 || dstN <= 0) {
        return;
    }
    if (srcN == dstN) {
        for (int i = 0; i < dstN; ++i) {
            dst[i] = src[i];
        }
        return;
    }
    const float scale = static_cast<float>(srcN) / static_cast<float>(dstN);
    for (int i = 0; i < dstN; ++i) {
        // Sample at the centre of the destination pixel.
        const float centre = (static_cast<float>(i) + 0.5f) * scale - 0.5f;
        const int base = static_cast<int>(std::floor(centre));
        const float frac = centre - static_cast<float>(base);
        float w[4];
        catmullRomWeights(frac, w);
        float acc = 0.0f;
        for (int k = 0; k < 4; ++k) {
            acc += w[k] * src[clampIndex(base - 1 + k, srcN)];
        }
        dst[i] = std::min(1.0f, std::max(0.0f, acc));
    }
}

/// Generic separable resample over a strided single-channel buffer.
void resampleSeparable(const float* src, int srcW, int srcH, float* dst, int dstW, int dstH,
                       std::vector<float>& scratch) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return;
    }
    scratch.assign(static_cast<std::size_t>(dstW) * srcH, 0.0f);
    // Horizontal pass: srcH rows of srcW -> dstW.
    for (int y = 0; y < srcH; ++y) {
        resampleRow1D(src + static_cast<std::size_t>(y) * srcW, srcW,
                      scratch.data() + static_cast<std::size_t>(y) * dstW, dstW);
    }
    // Vertical pass: srcH rows -> dstH rows, column by column.
    std::vector<float> column(static_cast<std::size_t>(srcH));
    std::vector<float> out(static_cast<std::size_t>(dstH));
    for (int x = 0; x < dstW; ++x) {
        for (int y = 0; y < srcH; ++y) {
            column[y] = scratch[static_cast<std::size_t>(y) * dstW + x];
        }
        resampleRow1D(column.data(), srcH, out.data(), dstH);
        for (int y = 0; y < dstH; ++y) {
            dst[static_cast<std::size_t>(y) * dstW + x] = out[y];
        }
    }
}

} // namespace

void resizeBicubic(const Matte& src, Matte& dst) {
    if (src.empty() || dst.empty()) {
        return;
    }
    std::vector<float> scratch;
    resampleSeparable(src.data(), src.width(), src.height(), dst.data(), dst.width(), dst.height(),
                      scratch);
}

void resizeBilinear(const Matte& src, Matte& dst) {
    if (src.empty() || dst.empty()) {
        return;
    }
    const int sw = src.width();
    const int sh = src.height();
    const int dw = dst.width();
    const int dh = dst.height();
    const float sx = static_cast<float>(sw) / static_cast<float>(dw);
    const float sy = static_cast<float>(sh) / static_cast<float>(dh);
    for (int y = 0; y < dh; ++y) {
        const float cy = (static_cast<float>(y) + 0.5f) * sy - 0.5f;
        const int y0 = clampIndex(static_cast<int>(std::floor(cy)), sh);
        const int y1 = clampIndex(y0 + 1, sh);
        const float fy = std::min(1.0f, std::max(0.0f, cy - static_cast<float>(y0)));
        float* drow = dst.row(y);
        for (int x = 0; x < dw; ++x) {
            const float cx = (static_cast<float>(x) + 0.5f) * sx - 0.5f;
            const int x0 = clampIndex(static_cast<int>(std::floor(cx)), sw);
            const int x1 = clampIndex(x0 + 1, sw);
            const float fx = std::min(1.0f, std::max(0.0f, cx - static_cast<float>(x0)));
            const float top = src.at(x0, y0) * (1.0f - fx) + src.at(x1, y0) * fx;
            const float bot = src.at(x0, y1) * (1.0f - fx) + src.at(x1, y1) * fx;
            drow[x] = std::min(1.0f, std::max(0.0f, top * (1.0f - fy) + bot * fy));
        }
    }
}

void resizeArea(const Matte& src, Matte& dst) {
    if (src.empty() || dst.empty()) {
        return;
    }
    const int sw = src.width();
    const int sh = src.height();
    const int dw = dst.width();
    const int dh = dst.height();
    if (dw > sw || dh > sh) {
        // Not a downscale; bicubic is the better kernel here.
        resizeBicubic(src, dst);
        return;
    }
    // Map each destination pixel to its (possibly fractional) source footprint and average.
    for (int dy = 0; dy < dh; ++dy) {
        const float sy0 = static_cast<float>(dy) * sh / dh;
        const float sy1 = static_cast<float>(dy + 1) * sh / dh;
        const int iy0 = static_cast<int>(std::floor(sy0));
        const int iy1 = std::max(iy0 + 1, static_cast<int>(std::ceil(sy1)));
        float* drow = dst.row(dy);
        for (int dx = 0; dx < dw; ++dx) {
            const float sx0 = static_cast<float>(dx) * sw / dw;
            const float sx1 = static_cast<float>(dx + 1) * sw / dw;
            const int ix0 = static_cast<int>(std::floor(sx0));
            const int ix1 = std::max(ix0 + 1, static_cast<int>(std::ceil(sx1)));
            double sum = 0.0;
            double weight = 0.0;
            for (int y = iy0; y < iy1 && y < sh; ++y) {
                const float wy = std::min(static_cast<float>(y + 1), sy1) -
                                 std::max(static_cast<float>(y), sy0);
                if (wy <= 0.0f) {
                    continue;
                }
                const float* srow = src.row(y);
                for (int x = ix0; x < ix1 && x < sw; ++x) {
                    const float wx = std::min(static_cast<float>(x + 1), sx1) -
                                     std::max(static_cast<float>(x), sx0);
                    if (wx <= 0.0f) {
                        continue;
                    }
                    const double w = static_cast<double>(wx) * wy;
                    sum += srow[x] * w;
                    weight += w;
                }
            }
            drow[dx] = (weight > 0.0) ? static_cast<float>(sum / weight) : 0.0f;
        }
    }
}

void resizeArea(const FloatImage& src, FloatImage& dst) {
    if (src.empty() || dst.empty()) {
        return;
    }
    const int sw = src.width();
    const int sh = src.height();
    const int dw = dst.width();
    const int dh = dst.height();
    for (int dy = 0; dy < dh; ++dy) {
        const float sy0 = static_cast<float>(dy) * sh / dh;
        const float sy1 = static_cast<float>(dy + 1) * sh / dh;
        const int iy0 = static_cast<int>(std::floor(sy0));
        const int iy1 = std::max(iy0 + 1, static_cast<int>(std::ceil(sy1)));
        float* drow = dst.row(dy);
        for (int dx = 0; dx < dw; ++dx) {
            const float sx0 = static_cast<float>(dx) * sw / dw;
            const float sx1 = static_cast<float>(dx + 1) * sw / dw;
            const int ix0 = static_cast<int>(std::floor(sx0));
            const int ix1 = std::max(ix0 + 1, static_cast<int>(std::ceil(sx1)));
            double sum[4] = {0.0, 0.0, 0.0, 0.0};
            double weight = 0.0;
            for (int y = iy0; y < iy1 && y < sh; ++y) {
                const float wy = std::min(static_cast<float>(y + 1), sy1) -
                                 std::max(static_cast<float>(y), sy0);
                if (wy <= 0.0f) {
                    continue;
                }
                const float* srow = src.row(y);
                for (int x = ix0; x < ix1 && x < sw; ++x) {
                    const float wx = std::min(static_cast<float>(x + 1), sx1) -
                                     std::max(static_cast<float>(x), sx0);
                    if (wx <= 0.0f) {
                        continue;
                    }
                    const double w = static_cast<double>(wx) * wy;
                    const float* sp = srow + static_cast<std::size_t>(x) * 4u;
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += sp[c] * w;
                    }
                    weight += w;
                }
            }
            float* dp = drow + static_cast<std::size_t>(dx) * 4u;
            if (weight > 0.0) {
                for (int c = 0; c < 4; ++c) {
                    dp[c] = static_cast<float>(sum[c] / weight);
                }
            } else {
                dp[0] = dp[1] = dp[2] = dp[3] = 0.0f;
            }
        }
    }
}

int effectiveMatteResolution(int frameLongEdge, int requested) noexcept {
    if (requested <= 0) {
        requested = 512;
    }
    if (frameLongEdge <= 0) {
        return requested;
    }
    return std::min(frameLongEdge, requested);
}

void fitAndAlign(int width, int height, int maxEdge, int alignment, int& outW,
                 int& outH) noexcept {
    if (alignment < 1) {
        alignment = 1;
    }
    if (width < 1 || height < 1) {
        outW = alignment;
        outH = alignment;
        return;
    }
    const int longEdge = std::max(width, height);
    double scale = 1.0;
    if (maxEdge > 0 && longEdge > maxEdge) {
        scale = static_cast<double>(maxEdge) / static_cast<double>(longEdge);
    }
    int w = static_cast<int>(std::round(width * scale));
    int h = static_cast<int>(std::round(height * scale));
    // Round down to the alignment grid, but never below one cell.
    w = std::max(alignment, (w / alignment) * alignment);
    h = std::max(alignment, (h / alignment) * alignment);
    outW = w;
    outH = h;
}

} // namespace aibgr

namespace aibgr {
void resizeEdgeAware(const Matte& src, const FloatImage& lowGuide, const FloatImage& highGuide, Matte& dst) {
    if (src.empty() || dst.empty() || src.width()!=lowGuide.width() || src.height()!=lowGuide.height() ||
        dst.width()!=highGuide.width() || dst.height()!=highGuide.height()) return;
    auto channel=[](float v) { return std::isfinite(v) ? std::clamp(v,0.0f,1.0f) : 0.0f; };
    for(int y=0;y<dst.height();++y) {
        const float sy=(y+0.5f)*src.height()/dst.height()-0.5f;
        const int ybase=static_cast<int>(std::floor(sy)); const float fy=sy-ybase;
        for(int x=0;x<dst.width();++x) {
            const float sx=(x+0.5f)*src.width()/dst.width()-0.5f;
            const int xbase=static_cast<int>(std::floor(sx)); const float fx=sx-xbase;
            const float* hp=highGuide.pixel(x,y); float sum=0, weights=0;
            for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
                const int xx=std::clamp(xbase+i,0,src.width()-1), yy=std::clamp(ybase+j,0,src.height()-1);
                const float* lp=lowGuide.pixel(xx,yy); float difference=0;
                for(int c=0;c<3;++c) { const float d=channel(hp[c])-channel(lp[c]); difference+=d*d; }
                const float spatial=(i ? fx : 1-fx)*(j ? fy : 1-fy);
                // Bounded bilateral weighting preserves high-contrast edges without a per-pixel exp.
                const float weight=spatial/(1.0f+40.0f*difference+400.0f*difference*difference);
                weights+=weight; sum+=weight*src.at(xx,yy);
            }
            dst.at(x,y)=weights>0 ? std::clamp(sum/weights,0.0f,1.0f) : 0.0f;
        }
    }
}
} // namespace aibgr

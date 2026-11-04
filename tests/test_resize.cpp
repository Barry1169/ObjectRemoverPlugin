// SPDX-License-Identifier: MIT
//
// Resampling. The matte is computed at model resolution and brought back up to frame resolution, so
// the upscaler is what decides whether hair and fur survive. These tests check the properties that
// matter - energy conservation on the way down, no ringing outside [0,1] on the way up, and no
// accidental upscaling of the matte.

#include "TestCase.h"

#include <algorithm>
#include <cmath>

#include "core/FloatImage.h"
#include "matte/Matte.h"
#include "matte/Resize.h"

using namespace aibgr;

TEST(resize, areaDownscaleOfAConstantIsConstant) {
    Matte src(64, 48);
    src.fill(0.37f);
    Matte dst(16, 12);
    resizeArea(src, dst);

    for (int y = 0; y < dst.height(); ++y) {
        for (int x = 0; x < dst.width(); ++x) {
            CHECK_NEAR(dst.at(x, y), 0.37f, 1e-5);
        }
    }
}

TEST(resize, areaDownscaleConservesTheMean) {
    // Averaging must not brighten or darken the matte overall; a systematic shift would move the
    // whole silhouette in or out.
    Matte src(64, 64);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            src.at(x, y) = ((x / 8 + y / 8) % 2 == 0) ? 1.0f : 0.0f; // coarse checkerboard
        }
    }

    Matte dst(16, 16);
    resizeArea(src, dst);

    CHECK_NEAR(dst.mean(), src.mean(), 1e-3);
    // And a 2x2-aligned checkerboard averages to exactly half.
    CHECK_NEAR(dst.mean(), 0.5, 1e-3);
}

TEST(resize, areaDownscaleOfAFineCheckerboardIsMidGrey) {
    // The worst case for a naive point sampler, which would alias to solid black or solid white.
    Matte src(32, 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            src.at(x, y) = ((x + y) % 2 == 0) ? 1.0f : 0.0f;
        }
    }

    Matte dst(8, 8);
    resizeArea(src, dst);
    CHECK_NEAR(dst.mean(), 0.5, 0.02);
}

TEST(resize, areaFallsBackToBicubicWhenUpscaling) {
    // resizeArea is documented to handle an upscale rather than produce garbage, because the caller
    // decides the sizes and a mismatch is a logic error, not a crash.
    Matte src(4, 4);
    src.fill(0.6f);
    Matte dst(16, 16);
    resizeArea(src, dst);

    CHECK_EQ(dst.width(), 16);
    CHECK_NEAR(dst.mean(), 0.6f, 1e-3);
}

TEST(resize, bicubicUpscaleOfAConstantIsConstant) {
    Matte src(8, 8);
    src.fill(0.42f);
    Matte dst(64, 64);
    resizeBicubic(src, dst);

    // Interior samples must be exact; the outermost half-pixel can be affected by clamping, so it is
    // checked with a looser tolerance rather than excluded silently.
    for (int y = 1; y < dst.height() - 1; ++y) {
        for (int x = 1; x < dst.width() - 1; ++x) {
            CHECK_NEAR(dst.at(x, y), 0.42f, 1e-4);
        }
    }
    CHECK_NEAR(dst.mean(), 0.42f, 1e-3);
}

TEST(resize, bicubicStaysInsideUnitRangeOnAStepEdge) {
    // Catmull-Rom overshoots by design. Without clamping a hard silhouette edge would ring above 1
    // and below 0, which shows up as a dark or bright halo around the subject - the exact artifact
    // the refinement stage exists to avoid.
    Matte src(16, 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            src.at(x, y) = (x < 8) ? 0.0f : 1.0f;
        }
    }

    Matte dst(64, 64);
    resizeBicubic(src, dst);

    float lo = 2.0f;
    float hi = -1.0f;
    for (int y = 0; y < dst.height(); ++y) {
        for (int x = 0; x < dst.width(); ++x) {
            lo = std::min(lo, dst.at(x, y));
            hi = std::max(hi, dst.at(x, y));
        }
    }
    CHECK_MSG(lo >= -1e-6f, "bicubic undershot below 0: " + std::to_string(lo));
    CHECK_MSG(hi <= 1.0f + 1e-6f, "bicubic overshot above 1: " + std::to_string(hi));
}

TEST(resize, bicubicIsSharperThanBilinearOnADiagonal) {
    // A diagonal edge is where the two resamplers differ most. Bicubic reconstructs the edge, so its
    // transition band is NARROWER than bilinear's - bilinear simply blurs across it. Narrower is what
    // we want: the matte is upscaled from model resolution, and a wide band is a soft, mushy
    // silhouette that loses hair. Both must still be non-degenerate, i.e. neither is point sampling.
    Matte src(16, 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            src.at(x, y) = (x < y) ? 1.0f : 0.0f;
        }
    }

    Matte cubic(64, 64);
    Matte linear(64, 64);
    resizeBicubic(src, cubic);
    resizeBilinear(src, linear);

    // Count values that are neither near 0 nor near 1: the soft transition band.
    auto bandCount = [](const Matte& m) {
        int n = 0;
        for (int y = 0; y < m.height(); ++y) {
            for (int x = 0; x < m.width(); ++x) {
                const float v = m.at(x, y);
                if (v > 0.05f && v < 0.95f) {
                    ++n;
                }
            }
        }
        return n;
    };
    CHECK(bandCount(cubic) > 0);
    CHECK(bandCount(linear) > 0);
    CHECK_MSG(bandCount(cubic) <= bandCount(linear),
              "bicubic transition band (" + std::to_string(bandCount(cubic)) +
                  ") was wider than bilinear (" + std::to_string(bandCount(linear)) + ")");
}

TEST(resize, bilinearUpscaleHitsTheSourceTexels) {
    Matte src(2, 2);
    src.at(0, 0) = 0.0f;
    src.at(1, 0) = 1.0f;
    src.at(0, 1) = 0.0f;
    src.at(1, 1) = 1.0f;

    Matte dst(4, 4);
    resizeBilinear(src, dst);

    CHECK_NEAR(dst.at(0, 0), 0.0f, 1e-5);
    CHECK_NEAR(dst.at(3, 0), 1.0f, 1e-5);
    CHECK_NEAR(dst.mean(), 0.5f, 0.05);
}

TEST(resize, floatImageAreaDownscale) {
    // The image path is used to bring the frame down to model resolution before inference.
    FloatImage src(8, 8);
    src.fill(0.2f, 0.4f, 0.6f, 1.0f);
    FloatImage dst(2, 2);
    resizeArea(src, dst);

    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            CHECK_NEAR(dst.pixel(x, y)[0], 0.2f, 1e-5);
            CHECK_NEAR(dst.pixel(x, y)[1], 0.4f, 1e-5);
            CHECK_NEAR(dst.pixel(x, y)[2], 0.6f, 1e-5);
            CHECK_NEAR(dst.pixel(x, y)[3], 1.0f, 1e-5);
        }
    }
}

TEST(resize, effectiveResolutionNeverUpscales) {
    // Asking for a 1024 matte on a 400-pixel frame must not invent detail; it would only cost time
    // and blur the result on the way back down.
    CHECK_EQ(effectiveMatteResolution(400, 1024), 400);
    CHECK_EQ(effectiveMatteResolution(1920, 512), 512);
    CHECK_EQ(effectiveMatteResolution(512, 512), 512);

    // A non-positive frame size means "unknown", not "zero": returning 0 here would make every
    // downstream buffer empty and the render would produce nothing. The request is used instead.
    CHECK_EQ(effectiveMatteResolution(0, 512), 512);
    CHECK_EQ(effectiveMatteResolution(-100, 384), 384);
    // A non-positive request means "unspecified" and falls back to the default working resolution.
    CHECK(effectiveMatteResolution(1920, 0) > 0);
    CHECK(effectiveMatteResolution(1920, 0) <= 1920);
}

TEST(resize, fitAndAlignRespectsTheGridAndTheCap) {
    int w = 0;
    int h = 0;

    // 16:9 frame into a 512 long-edge cap on a 32-pixel grid (BiRefNet's Swin window size).
    fitAndAlign(1920, 1080, 512, 32, w, h);
    CHECK(w <= 512);
    CHECK_EQ(w % 32, 0);
    CHECK_EQ(h % 32, 0);
    CHECK(w > 0 && h > 0);
    // Aspect is preserved to within one grid cell.
    const double target = 1080.0 / 1920.0;
    CHECK(std::fabs(static_cast<double>(h) / w - target) < 32.0 / 512.0 + 1e-9);

    // A tiny frame must still produce at least one cell rather than 0x0, which would make every
    // downstream buffer empty.
    fitAndAlign(3, 5, 512, 32, w, h);
    CHECK(w >= 32);
    CHECK(h >= 32);

    // Portrait orientation: the cap applies to the LONG edge, whichever that is.
    fitAndAlign(1080, 1920, 512, 32, w, h);
    CHECK(h <= 512);
    CHECK(w < h);
}

TEST(resize, fitAndAlignWithNoAlignment) {
    int w = 0;
    int h = 0;
    fitAndAlign(1920, 1080, 512, 1, w, h);
    CHECK_EQ(w, 512);
    CHECK(h > 0 && h <= 512);
}

TEST(resize, sameSizeCopyIsLossless) {
    Matte src(17, 13);
    for (int y = 0; y < 13; ++y) {
        for (int x = 0; x < 17; ++x) {
            src.at(x, y) = static_cast<float>((x * 7 + y * 13) % 101) / 100.0f;
        }
    }

    Matte dst(17, 13);
    resizeBicubic(src, dst);
    for (int y = 0; y < 13; ++y) {
        for (int x = 0; x < 17; ++x) {
            CHECK_NEAR(dst.at(x, y), src.at(x, y), 1e-5);
        }
    }
}

TEST(resize, edgeAwareKeepsSharpColourBoundary) {
    aibgr::FloatImage high(16,4), low(4,1);
    for(int y=0;y<4;++y) for(int x=0;x<16;++x) {
        float* p=high.pixel(x,y); p[0]=x<8 ? 1.0f : 0.0f; p[1]=0; p[2]=x<8 ? 0.0f : 1.0f; p[3]=1;
    }
    aibgr::resizeArea(high,low);
    aibgr::Matte src(4,1), out(16,4); src.at(0,0)=src.at(1,0)=1; src.at(2,0)=src.at(3,0)=0;
    aibgr::resizeEdgeAware(src,low,high,out);
    CHECK(out.at(7,1)>0.99f); CHECK(out.at(8,1)<0.01f);
    src.fill(0.4f); aibgr::resizeEdgeAware(src,low,high,out);
    CHECK_NEAR(out.at(7,1),0.4,1e-6); CHECK_NEAR(out.at(8,1),0.4,1e-6);
}

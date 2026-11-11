// SPDX-License-Identifier: MIT
#include "core/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace aibgr {

namespace {

// IEC 61966-2-1 sRGB transfer function constants.
constexpr float kSrgbLinearCut = 0.0031308f;
constexpr float kSrgbLinearScale = 12.92f;
constexpr float kSrgbGammaScale = 1.055f;
constexpr float kSrgbGammaOffset = 0.055f;
constexpr float kSrgbInvGamma = 1.0f / 2.4f;

// ARRI LogC (EI 800) published constants, in the lettering ARRI uses:
//   encode(x) = x > cut ? c * log10(d*x + e) + b : a*x + b
//   decode(y) = y > a*cut + b ? (pow(10, (y - b)/c) - e) / d : (y - b)/a
// Sanity anchors: encode(1.0) ~= 0.5583 (LogC EI800 white), encode(0.18) ~= 0.3643.
constexpr float kLogCCut = 0.0113f;
constexpr float kLogCA = 0.00549158f;
constexpr float kLogCB = 0.24719f;
constexpr float kLogCC = 0.385537f;
constexpr float kLogCD = 5.36466f;
constexpr float kLogCE = 1.04668f;
/// The encoded value at which the curve switches from its linear segment to its log segment.
constexpr float kLogCVCut = kLogCA * kLogCCut + kLogCB;

// Rec.709 luminance coefficients.
constexpr float kLumR = 0.2126f;
constexpr float kLumG = 0.7152f;
constexpr float kLumB = 0.0722f;

} // namespace

const char* colorSpaceName(ColorSpaceAssumption c) noexcept {
    switch (c) {
    case ColorSpaceAssumption::Auto: return "Auto";
    case ColorSpaceAssumption::DisplayReferred: return "DisplayReferred";
    case ColorSpaceAssumption::SceneLinear: return "SceneLinear";
    case ColorSpaceAssumption::LogC: return "LogC";
    }
    return "?";
}

float srgbEncode(float linear) noexcept {
    if (!(linear > 0.0f)) { // NaN and negatives collapse to 0
        return 0.0f;
    }
    if (linear <= kSrgbLinearCut) {
        return linear * kSrgbLinearScale;
    }
    return kSrgbGammaScale * std::pow(linear, kSrgbInvGamma) - kSrgbGammaOffset;
}

float srgbDecode(float srgb) noexcept {
    if (!(srgb > 0.0f)) {
        return 0.0f;
    }
    if (srgb <= 0.04045f) {
        return srgb / kSrgbLinearScale;
    }
    return std::pow((srgb + kSrgbGammaOffset) / kSrgbGammaScale, 2.4f);
}

float logCToLinear(float logC) noexcept {
    if (!(logC > kLogCB)) {
        return 0.0f; // at or below the black point, and NaN
    }
    const float linear = (logC > kLogCVCut)
                             ? (std::pow(10.0f, (logC - kLogCB) / kLogCC) - kLogCE) / kLogCD
                             : (logC - kLogCB) / kLogCA;
    return std::max(0.0f, linear);
}

double meanLuminance(const FloatImage& image) noexcept {
    if (image.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    const std::size_t n = static_cast<std::size_t>(image.width()) * image.height();
    for (int y = 0; y < image.height(); ++y) {
        const float* p = image.row(y);
        for (int x = 0; x < image.width(); ++x, p += 4) {
            sum += static_cast<double>(p[0]) * kLumR + static_cast<double>(p[1]) * kLumG +
                   static_cast<double>(p[2]) * kLumB;
        }
    }
    return sum / static_cast<double>(n);
}

ColorSpaceAssumption autoDetect(const FloatImage& image) noexcept {
    const double lum = meanLuminance(image);
    // Threshold chosen between the two clusters described in the header.
    return (lum < 0.25) ? ColorSpaceAssumption::SceneLinear : ColorSpaceAssumption::DisplayReferred;
}

void toInferenceEncoding(FloatImage& image, ColorSpaceAssumption assumption) noexcept {
    if (image.empty()) {
        return;
    }
    if (assumption == ColorSpaceAssumption::Auto) {
        assumption = autoDetect(image);
    }
    if (assumption == ColorSpaceAssumption::DisplayReferred) {
        return; // already what the network wants
    }
    for (int y = 0; y < image.height(); ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width(); ++x, p += 4) {
            for (int c = 0; c < 3; ++c) {
                float v = p[c];
                if (assumption == ColorSpaceAssumption::LogC) {
                    v = logCToLinear(v);
                }
                p[c] = srgbEncode(v);
            }
            // p[3] (alpha) is deliberately untouched.
        }
    }
}

} // namespace aibgr

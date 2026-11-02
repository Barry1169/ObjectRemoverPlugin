// SPDX-License-Identifier: MIT
#include "plugin/OfxImageAdapter.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/FloatImage.h"
#include "core/PixelConvert.h"
#include "core/Premultiply.h"
#include "plugin/HostBridge.h"
#include "util/Log.h"

namespace aibgr {
namespace ofx {

namespace {

/// Field identifiers as integers, matching FrameContext::fieldToRender's documented encoding.
constexpr int kFieldNone = 0;
constexpr int kFieldBoth = 1;
constexpr int kFieldLower = 2;
constexpr int kFieldUpper = 3;

int parseField(const std::string& value) noexcept {
    if (value == kOfxImageFieldBoth) {
        return kFieldBoth;
    }
    if (value == kOfxImageFieldLower) {
        return kFieldLower;
    }
    if (value == kOfxImageFieldUpper) {
        return kFieldUpper;
    }
    return kFieldNone; // kOfxImageFieldNone, or anything unrecognised
}

ComponentSet parseComponents(const std::string& value) noexcept {
    if (value == kOfxImageComponentRGBA) {
        return ComponentSet::RGBA;
    }
    if (value == kOfxImageComponentRGB) {
        return ComponentSet::RGB;
    }
    if (value == kOfxImageComponentAlpha) {
        return ComponentSet::Alpha;
    }
    return ComponentSet::None; // kOfxImageComponentNone, or a custom component set we cannot read
}

/// OFX bit-depth string -> BitDepth. Lives here rather than in core/ because core/ must not
/// include an OFX header; the mapping is the one piece of host vocabulary that cannot be shared.
BitDepth parseBitDepth(const std::string& value) noexcept {
    if (value == kOfxBitDepthByte) {
        return BitDepth::Byte;
    }
    if (value == kOfxBitDepthShort) {
        return BitDepth::Short; // unsigned 16-bit INTEGER, not a float
    }
    if (value == kOfxBitDepthHalf) {
        return BitDepth::Half;
    }
    if (value == kOfxBitDepthFloat) {
        return BitDepth::Float;
    }
    return BitDepth::None;
}

/// Reusable per-thread scratch for the output premultiply pass.
///
/// thread_local so concurrent renders on different threads never share it; sized on demand and
/// never shrunk, so after warm-up this costs nothing per frame.
std::vector<float>& premultScratch() {
    static thread_local std::vector<float> scratch;
    return scratch;
}

/// Reads one pixel of any supported layout into straight float RGBA.
void readPixel(const OfxImageInfo& src, const void* pixel, float* outRGBA) noexcept {
    switch (src.componentCount) {
    case 4:
        componentsToFloat(pixel, src.bitDepth, outRGBA, 4);
        break;
    case 3:
        // No alpha channel means fully opaque; that is what the spec says to assume.
        componentsToFloat(pixel, src.bitDepth, outRGBA, 3);
        outRGBA[3] = 1.0f;
        break;
    case 1:
        // A single-component image is a matte. Presenting it as opaque grey keeps the geometry
        // and luminance intact for the segmentation stage.
        componentsToFloat(pixel, src.bitDepth, outRGBA, 1);
        outRGBA[1] = outRGBA[0];
        outRGBA[2] = outRGBA[0];
        outRGBA[3] = 1.0f;
        break;
    default:
        outRGBA[0] = outRGBA[1] = outRGBA[2] = outRGBA[3] = 0.0f;
        break;
    }
}

/// Writes one straight float RGBA pixel into any supported destination layout.
///
/// A one-component destination receives the red channel. That is deliberate: Matte Only puts the
/// matte in RGB with alpha 1, so red is the matte, and it is the only mode where an Alpha output
/// clip makes sense.
void writePixel(const OfxImageInfo& dst, void* pixel, const float* rgba) noexcept {
    const int count = dst.componentCount >= 3 ? dst.componentCount : 1;
    componentsFromFloat(rgba, pixel, dst.bitDepth, count);
}

} // namespace

// ---------------------------------------------------------------------------
// OfxPremult
// ---------------------------------------------------------------------------

OfxPremult parsePremult(const std::string& value) noexcept {
    if (value == kOfxImagePreMultiplied) {
        return OfxPremult::Premultiplied;
    }
    if (value == kOfxImageUnPreMultiplied) {
        return OfxPremult::Unpremultiplied;
    }
    if (value == kOfxImageOpaque) {
        return OfxPremult::Opaque;
    }
    return OfxPremult::Unknown;
}

const char* premultName(OfxPremult p) noexcept {
    switch (p) {
    case OfxPremult::Opaque: return "Opaque";
    case OfxPremult::Premultiplied: return "Premultiplied";
    case OfxPremult::Unpremultiplied: return "Unpremultiplied";
    case OfxPremult::Unknown: break;
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// OfxImageInfo
// ---------------------------------------------------------------------------

void* OfxImageInfo::pixelAddress(int x, int y) const noexcept {
    if (x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2 || pixelBytes <= 0) {
        return nullptr;
    }
    // Signed on purpose: rowBytes may be negative for a top-down host buffer, and (y - y1) * a
    // negative stride must not wrap through an unsigned type.
    char* base = static_cast<char*>(data);
    base += static_cast<std::ptrdiff_t>(y - bounds.y1) * rowBytes;
    base += static_cast<std::ptrdiff_t>(x - bounds.x1) * pixelBytes;
    return static_cast<void*>(base);
}

OfxImageInfo inspectImage(OfxPropertySetHandle imageProps) noexcept {
    OfxImageInfo info;
    HostBridge& bridge = HostBridge::instance();
    const Props p(bridge.props(), imageProps);
    if (!p.valid()) {
        AIBGR_LOGE("inspectImage: null image property set");
        return info;
    }

    // Components and pixel depth use the kOfxImageEffectProp* spelling even on an image; this is
    // what the SDK's own ImageBase constructor reads.
    const std::string components = p.getString(kOfxImageEffectPropComponents, 0);
    info.components = parseComponents(components);
    info.componentCount = componentCount(info.components);

    const std::string depth = p.getString(kOfxImageEffectPropPixelDepth, 0);
    info.bitDepth = parseBitDepth(depth);

    info.premultiplication = parsePremult(p.getString(kOfxImageEffectPropPreMultiplication, 0));
    info.rowBytes = static_cast<std::ptrdiff_t>(p.getInt(kOfxImagePropRowBytes, 0, 0));
    info.pixelAspectRatio = p.getDouble(kOfxImagePropPixelAspectRatio, 0, 1.0);
    info.data = p.getPointer(kOfxImagePropData, 0);
    info.uniqueId = p.getString(kOfxImagePropUniqueIdentifier, 0);

    const OfxRectI b = p.getRectI(kOfxImagePropBounds);
    info.bounds = RectI{b.x1, b.y1, b.x2, b.y2};

    // pixelBytes is derived rather than read: the spec guarantees rowBytes >= pixelBytes * width,
    // and no property states the per-pixel size directly.
    const int componentBytes = bitDepthComponentBytes(info.bitDepth);
    info.pixelBytes = componentBytes * info.componentCount;

    if (info.data == nullptr) {
        AIBGR_LOGE("inspectImage: host returned no data pointer");
    } else if (info.pixelBytes <= 0) {
        AIBGR_LOGE("inspectImage: unsupported layout components='%s' depth='%s'", components.c_str(),
                   depth.c_str());
    } else if (info.bounds.empty()) {
        AIBGR_LOGE("inspectImage: empty bounds");
    } else if (std::abs(info.rowBytes) < static_cast<int>(info.pixelBytes) * info.bounds.width()) {
        // The spec says rowBytes is at least sizeof(pixel) * width. A host that violates this
        // would let us read outside its buffer, so refuse rather than crash Resolve.
        AIBGR_LOGE("inspectImage: rowBytes %ld too small for %d px of %d bytes (bounds %s)",
                   static_cast<long>(info.rowBytes), info.bounds.width(), info.pixelBytes,
                   rectToString(info.bounds).c_str());
        // Zeroing data makes valid() false, so callers take the graceful path instead of
        // dereferencing a buffer we cannot safely walk.
        info.data = nullptr;
    }
    return info;
}

// ---------------------------------------------------------------------------
// readImageToFloat
// ---------------------------------------------------------------------------

Result readImageToFloat(const OfxImageInfo& src, const RectI& window, FloatImage& out,
                        bool unpremultiply) {
    if (!src.valid()) {
        out.resize(0, 0);
        return Result::fail(Status::InvalidArgument, "cannot read an invalid OFX image");
    }
    if (window.empty()) {
        out.resize(0, 0);
        return Result::success(); // Nothing to do is not an error.
    }

    out.resize(window.width(), window.height());
    if (static_cast<int>(out.height()) != window.height() ||
        static_cast<int>(out.width()) != window.width()) {
        return Result::fail(Status::OutOfMemory, "float buffer resize failed");
    }

    const int width = window.width();
    bool outside = false;

    for (int y = window.y1; y < window.y2; ++y) {
        float* dstRow = out.row(y - window.y1);
        for (int x = window.x1; x < window.x2; ++x) {
            float* d = dstRow + static_cast<std::size_t>(x - window.x1) * FloatImage::kChannels;
            const void* pixel = src.pixelAddress(x, y);
            if (pixel == nullptr) {
                // Outside the source bounds. The spec's instruction for a missing image is to
                // assume black and transparent.
                d[0] = d[1] = d[2] = d[3] = 0.0f;
                outside = true;
            } else {
                readPixel(src, pixel, d);
            }
        }
        if (unpremultiply) {
            unpremultiplyRow(dstRow, width);
        }
    }

    if (outside) {
        AIBGR_LOGD("readImageToFloat: window %s extends past source bounds %s; "
                   "uncovered pixels are transparent black",
                   rectToString(window).c_str(), rectToString(src.bounds).c_str());
    }
    return Result::success();
}

// ---------------------------------------------------------------------------
// writeFloatToImage
// ---------------------------------------------------------------------------

Result writeFloatToImage(const OfxImageInfo& dst, const RectI& window, const FloatImage& in,
                         OfxPremult wantPremult) {
    if (!dst.valid()) {
        return Result::fail(Status::InvalidArgument, "cannot write to an invalid OFX image");
    }
    if (window.empty()) {
        return Result::success();
    }
    if (in.empty()) {
        return Result::fail(Status::InvalidArgument, "nothing to write: source buffer is empty");
    }
    if (static_cast<int>(in.width()) != window.width() ||
        static_cast<int>(in.height()) != window.height()) {
        return Result::fail(Status::InvalidArgument,
                            "source buffer size does not match the render window");
    }

    const bool premultiplyOut = (wantPremult == OfxPremult::Premultiplied);
    const int width = window.width();

    for (int y = window.y1; y < window.y2; ++y) {
        const float* srcRow = in.row(y - window.y1);

        // Premultiplication is applied per row into a small reusable scratch buffer so we never
        // mutate the caller's FloatImage, which may still be needed (it is the pipeline's output).
        if (premultiplyOut) {
            std::vector<float>& scratch = premultScratch();
            const std::size_t floats = static_cast<std::size_t>(width) * FloatImage::kChannels;
            if (scratch.size() < floats) {
                scratch.resize(floats);
            }
            std::memcpy(scratch.data(), srcRow, floats * sizeof(float));
            premultiplyRow(scratch.data(), width);
            srcRow = scratch.data();
        }

        for (int x = window.x1; x < window.x2; ++x) {
            void* pixel = dst.pixelAddress(x, y);
            if (pixel == nullptr) {
                continue; // Outside the destination bounds; nothing legal to write there.
            }
            const float* s = srcRow + static_cast<std::size_t>(x - window.x1) * FloatImage::kChannels;
            writePixel(dst, pixel, s);
        }
    }
    return Result::success();
}

// ---------------------------------------------------------------------------
// Clip / render-argument helpers
// ---------------------------------------------------------------------------

OfxPremult readClipPremult(OfxPropertySetHandle clipProps) noexcept {
    HostBridge& bridge = HostBridge::instance();
    const Props p(bridge.props(), clipProps);
    if (!p.valid()) {
        return OfxPremult::Unknown;
    }
    return parsePremult(p.getString(kOfxImageEffectPropPreMultiplication, 0));
}

Result readFrameContext(OfxPropertySetHandle inArgs, OfxPropertySetHandle effectProps, double time,
                        FrameContext& out) noexcept {
    out = FrameContext{};
    out.time = time;

    HostBridge& bridge = HostBridge::instance();

    if (inArgs != nullptr) {
        const Props a(bridge.props(), inArgs);

        const OfxPointD scale = a.getPointD(kOfxImageEffectPropRenderScale);
        if (scale.x > 0.0 && scale.y > 0.0) {
            out.renderScaleX = scale.x;
            out.renderScaleY = scale.y;
        }

        const OfxRectI rw = a.getRectI(kOfxImageEffectPropRenderWindow);
        // Only adopt it if the host actually populated it; otherwise the caller falls back to the
        // full render bounds, which is what an absent window means.
        if (rw.x2 > rw.x1 && rw.y2 > rw.y1) {
            out.renderWindow = RectI{rw.x1, rw.y1, rw.x2, rw.y2};
        }

        out.sequentialRenderStatus =
            (a.getInt(kOfxImageEffectPropSequentialRenderStatus, 0, 0) != 0);
        out.interactiveRender = (a.getInt(kOfxImageEffectPropInteractiveRenderStatus, 0, 0) != 0);
        out.draftQuality = (a.getInt(kOfxImageEffectPropRenderQualityDraft, 0, 0) != 0);

        // ThumbnailRender is optional: only present on hosts that implement it.
        if (a.dimension(kOfxImageEffectPropThumbnailRender) > 0) {
            out.thumbnailRender = (a.getInt(kOfxImageEffectPropThumbnailRender, 0, 0) != 0);
        }

        out.fieldToRender = parseField(a.getString(kOfxImageEffectPropFieldToRender, 0));
    }

    if (effectProps != nullptr) {
        const Props e(bridge.props(), effectProps);
        const double fps = e.getDouble(kOfxImageEffectPropFrameRate, 0, 0.0);
        if (fps > 0.0) {
            out.frameRate = fps;
        }
        const double par = e.getDouble(kOfxImageEffectPropProjectPixelAspectRatio, 0, 0.0);
        if (par > 0.0) {
            out.pixelAspectX = par;
            out.pixelAspectY = 1.0;
        }
    }

    if (out.renderWindow.empty()) {
        return Result::fail(Status::NotFound, "host reported no render window");
    }
    return Result::success();
}

} // namespace ofx
} // namespace aibgr

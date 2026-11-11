// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <string>

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include "core/FloatImage.h"
#include "core/FrameContext.h"
#include "core/PipelineSettings.h"
#include "core/Types.h"
#include "util/Status.h"

namespace aibgr {
namespace ofx {

/// Premultiplication state as reported by the host. The SDK spells these `kOfxImageOpaque`,
/// `kOfxImagePreMultiplied` and `kOfxImageUnPreMultiplied`; they are string-valued properties.
enum class OfxPremult { Opaque, Premultiplied, Unpremultiplied, Unknown };

OfxPremult parsePremult(const std::string& value) noexcept;
const char* premultName(OfxPremult p) noexcept;

/// Everything needed to address the pixels of one OFX image, gathered from its property set.
///
/// The property names follow the SDK's own ImageBase constructor: components and pixel depth are
/// advertised under the `kOfxImageEffectProp*` spelling even on an image, which is easy to get
/// wrong.
struct OfxImageInfo {
    void* data = nullptr;
    std::ptrdiff_t rowBytes = 0; ///< may legitimately be negative (top-down host buffers)
    int pixelBytes = 0;          ///< bytes per pixel across all components
    int componentCount = 0;      ///< 1 = Alpha, 3 = RGB, 4 = RGBA
    RectI bounds;
    BitDepth bitDepth = BitDepth::Float;
    ComponentSet components = ComponentSet::None;
    OfxPremult premultiplication = OfxPremult::Unknown;
    double pixelAspectRatio = 1.0;
    std::string uniqueId;

    bool valid() const noexcept { return data != nullptr && pixelBytes > 0 && !bounds.empty(); }

    /// Address of pixel (x, y) in OFX pixel coordinates, or nullptr if outside bounds.
    ///
    /// `(y - y1) * rowBytes` is computed in ptrdiff_t on purpose: the SDK casts to size_t and
    /// relies on wraparound, which is correct but fragile. Signed arithmetic handles negative
    /// rowBytes directly.
    ///
    /// A single const member returning void* rather than a const/non-const pair: the two would
    /// differ only in return type, which is not a legal overload, and this struct owns no pixels
    /// anyway - it points into a host buffer.
    void* pixelAddress(int x, int y) const noexcept;
};

OfxImageInfo inspectImage(OfxPropertySetHandle imageProps) noexcept;

/// Reads `window` out of an OFX image into a straight-alpha float RGBA buffer.
///
/// `out` is resized to exactly the window's dimensions. Pixels inside the window but outside the
/// source bounds become transparent black, which is what the spec says to assume when an image
/// cannot be fetched. Integer depths (Byte/Short) are normalised; Half is converted properly.
Result readImageToFloat(const OfxImageInfo& src, const RectI& window, FloatImage& out,
                        bool unpremultiply);

/// Writes a straight-alpha float RGBA buffer into an OFX image, over `window`.
///
/// @param wantPremult the premultiplication the host expects on this output clip. Unknown means
///                    "leave straight", which is correct for an opaque image.
Result writeFloatToImage(const OfxImageInfo& dst, const RectI& window, const FloatImage& in,
                         OfxPremult wantPremult);

/// Reads the premultiplication advertised on a clip.
OfxPremult readClipPremult(OfxPropertySetHandle clipProps) noexcept;

/// Assembles a FrameContext from the render-action in-args plus the effect's project properties.
///
/// Every property is optional: anything the host omits keeps its FrameContext default, so a
/// partial or unusual host degrades instead of failing.
Result readFrameContext(OfxPropertySetHandle inArgs, OfxPropertySetHandle effectProps,
                        double time, FrameContext& out) noexcept;

} // namespace ofx
} // namespace aibgr

// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"

namespace aibgr {

/// Everything the pipeline needs to know about one render call, reduced to plain data.
///
/// Filled in by plugin/OfxImageAdapter from the OFX render arguments and image properties. The
/// pipeline never touches an OFX type, which is what keeps it testable headless.
struct FrameContext {
    /// Canonical time of the frame being rendered (kOfxPropTime).
    double time = 0.0;
    /// Source clip frame rate, used to decide whether the temporal stabiliser may treat two
    /// renders as adjacent frames. Falls back to 1.0 if the host does not report one.
    double frameRate = 1.0;

    /// kOfxImageEffectPropRenderScale. Resolve drops this below 1 during proxy playback.
    double renderScaleX = 1.0;
    double renderScaleY = 1.0;

    /// kOfxImageEffectPropRenderWindow: the sub-rectangle we are actually required to fill.
    /// Always a subset of `bounds`, but must be honoured on write.
    RectI renderWindow;
    /// The image's own bounds (OfxRectI from the image property set). Origin need not be (0,0).
    RectI bounds;
    /// Region of definition in pixel coordinates.
    RectI rod;

    double pixelAspectX = 1.0;
    double pixelAspectY = 1.0;

    /// kOfxImageEffectPropSequentialRenderStatus: true only while the host guarantees it is
    /// rendering us in strict frame order on a single instance. The temporal stabiliser is
    /// allowed to use recurrent state ONLY when this is true.
    bool sequentialRenderStatus = false;
    bool interactiveRender = false;
    /// kOfxImageEffectPropRenderQualityDraft: the host wants speed over accuracy.
    bool draftQuality = false;
    /// kOfxImageEffectPropThumbnailRender (OFX 1.5, host-optional). The AI model must never be
    /// loaded for a thumbnail.
    bool thumbnailRender = false;
    /// Set by the host abort callback; the pipeline checks it between row bands.
    bool aborted = false;

    /// Field being rendered, 0 = full frame.
    int fieldToRender = 0;

    /// True when this render is part of a BeginSequenceRender/EndSequenceRender bracket.
    bool inSequence = false;

    /// Convenience: the effective scale the pipeline should assume for its own work.
    double effectiveRenderScale() const noexcept {
        return (renderScaleX + renderScaleY) * 0.5;
    }
};

} // namespace aibgr

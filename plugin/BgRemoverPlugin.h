// SPDX-License-Identifier: MIT
#pragma once

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

#include "core/Pipeline.h"
#include "core/PipelineSettings.h"
#include "inference/U2NetSegmenter.h"
#include "inference/BoxSelector.h"
#include "guidance/CorrectionMask.h"
#include "guidance/BrushStrokes.h"
#include "guidance/GuidedSelection.h"
#include "plugin/OfxImageAdapter.h"
#include "util/Status.h"

namespace aibgr {
namespace ofx {

/// Bridges Pipeline's AbortChecker to the host's `abort` callback.
///
/// The host function is called on every poll, which for a long segmentation means potentially
/// thousands of calls per frame; that is cheap (it is a flag read on the host side) and is the
/// only way to make the Cancel button work.
class OfxAbortChecker final : public AbortChecker {
public:
    explicit OfxAbortChecker(OfxImageEffectHandle effect) noexcept : effect_(effect) {}
    bool aborted() const noexcept override;

private:
    OfxImageEffectHandle effect_;
};

/// One live instance of the plugin.
///
/// Created on kOfxActionCreateInstance, destroyed on kOfxActionDestroyInstance, and indexed in a
/// process-wide registry by the opaque OFX handle so the flat C entry point can find it. Nothing
/// here is static or global except that registry: Resolve creates several instances of the same
/// plugin (one per timeline node, plus a describe-time descriptor) and they must not share state.
///
/// Render calls are declared instance-safe: different instances can render concurrently. A mutex
/// also protects this instance's scratch buffers against cache purges and concurrent renders.
class PluginInstance {
public:
    explicit PluginInstance(OfxImageEffectHandle effect);
    ~PluginInstance();

    PluginInstance(const PluginInstance&) = delete;
    PluginInstance& operator=(const PluginInstance&) = delete;

    OfxImageEffectHandle effect() const noexcept { return effect_; }

    /// kOfxActionInstanceChanged: a control moved, or a clip was (dis)connected.
    void instanceChanged(const char* paramName, double time);

    /// kOfxImageEffectActionRender. Does the whole frame: fetch, convert, run, composite, write.
    ///
    /// Never throws and never returns a failure for a *content* problem. Per decision D9 a missing
    /// model, a failed inference or an unsupported layout degrades to pass-through and still
    /// returns success, because a broken background remover must not take Resolve down with it.
    Result render(double time, OfxPropertySetHandle inArgs);

    /// kOfxImageEffectActionBeginSequenceRender / EndSequenceRender.
    void beginSequenceRender(OfxPropertySetHandle inArgs);
    void endSequenceRender();

    /// kOfxImageEffectActionGetClipPreferences: match the output clip to the source clip so the
    /// host never has to convert behind our back.
    void clipPreferences(OfxPropertySetHandle outArgs);

    /// kOfxImageEffectActionIsIdentity. True when the effect provably does nothing, so the host can
    /// bypass us entirely.
    bool isIdentity(double time, std::string& identityClip, double& identityTime) const;

    /// Property set of the effect itself, holding kOfxImageEffectPropContext and the project
    /// properties. Fetched once, valid for the instance's lifetime.
    OfxPropertySetHandle effectProps() const noexcept { return effectProps_; }
    OfxParamSetHandle paramSet() const noexcept { return paramSet_; }

    /// Push-button handling from instanceChanged.
    void clearCache();
    void resetTemporalState();
    void preloadModel();

    Pipeline& pipeline() noexcept { return *pipeline_; }

private:
    /// Reads every inspector control for `time` into a PipelineSettings.
    Result readSettings(double time, PipelineSettings& out, std::string& note) const;

    /// Resolves the Source and Output clip handles. Idempotent; safe to retry if a clip was not
    /// connected when the instance was created.
    void resolveClips();

    /// Chooses the premultiplication to write, honouring the inspector override.
    static OfxPremult resolveOutputPremult(const PipelineSettings& settings,
                                           OfxPremult sourcePremult) noexcept;

    OfxImageEffectHandle effect_;
    OfxPropertySetHandle effectProps_ = nullptr;
    OfxParamSetHandle paramSet_ = nullptr;

    /// Diagnostics control: when true a pipeline failure is returned to the host instead of being
    /// swallowed into a pass-through. Off in shipping builds, on for bring-up.
    std::atomic<bool> failLoudly_{false};
    std::unique_ptr<Pipeline> pipeline_;
    std::unique_ptr<U2NetSegmenter> aiSegmenter_;
    CorrectionMask guidance_;
    BrushCorrections brush_;
    GuidedSelection selection_;
    BoxSelector boxSelection_;
    bool boxLoadAttempted_ = false, boxLoaded_ = false;
    bool briaLoadAttempted_ = false, briaLoaded_ = false;
    bool aiLoadAttempted_ = false;
    OfxAbortChecker abortChecker_;

    /// Clip handles and their property sets, fetched once per instance. The handles stay valid for
    /// the lifetime of the instance, so this is not per-frame work.
    OfxImageClipHandle sourceClip_ = nullptr;
    OfxImageClipHandle outputClip_ = nullptr;
    OfxPropertySetHandle sourceClipProps_ = nullptr;
    OfxPropertySetHandle outputClipProps_ = nullptr;

    std::atomic<bool> inSequence_{false};
    std::atomic<bool> resetFrameTracking_{false};
    std::mutex scratchMutex_;
    double lastRenderedTime_ = -1e18;
    bool clipsResolved_ = false;

    /// Grow-only per-frame buffers, reused across renders.
    FloatImage srcFloat_;
    FloatImage dstFloat_;
    PipelineStats lastStats_;
};

/// Process-wide registry of live instances, keyed on the opaque OFX handle.
///
/// The alternative is stashing our pointer in kOfxPropInstanceData on the effect property set,
/// which is what the SDK's Support library does. A local map is used instead because it does not
/// depend on the host honouring that property, and because it keeps every OFX interaction in one
/// place.
PluginInstance* findInstance(const void* handle) noexcept;

/// The action dispatcher behind OfxPlugin::mainEntry.
///
/// Signature is fixed by ofxCore.h:86 - action FIRST, then the handle, then in/out args. Every
/// branch returns kOfxStatOK or kOfxStatFailed; kOfxStatReplyDefault is returned only for actions
/// we deliberately do not implement. Never for CreateInstance/DestroyInstance: the field guide
/// records Resolve Free reporting "No parameter is exposed to user" and never calling Render when
/// those two reply default.
OfxStatus pluginMain(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                     OfxPropertySetHandle outArgs) noexcept;

/// kOfxActionDescribe and kOfxImageEffectActionDescribeInContext.
OfxStatus describeEffect(OfxImageEffectHandle effect) noexcept;
OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) noexcept;

} // namespace ofx
} // namespace aibgr

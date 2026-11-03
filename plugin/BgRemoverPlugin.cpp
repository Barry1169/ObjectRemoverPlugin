// SPDX-License-Identifier: MIT
#include "plugin/BgRemoverPlugin.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <unordered_map>

#include "aibgr/PluginIdentity.h"
#include "composite/Compositor.h"
#include "plugin/HostBridge.h"
#include "plugin/ParamDefs.h"
#include "plugin/ParamValues.h"
#include "plugin/PluginPaths.h"
#include "util/Log.h"

namespace aibgr {
namespace ofx {

namespace {

// ---------------------------------------------------------------------------
// Instance registry
// ---------------------------------------------------------------------------

std::mutex& registryMutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<const void*, PluginInstance*>& registry() {
    static std::unordered_map<const void*, PluginInstance*> map;
    return map;
}

void registerInstance(const void* handle, PluginInstance* instance) {
    std::lock_guard<std::mutex> lock(registryMutex());
    registry()[handle] = instance;
}

void unregisterInstance(const void* handle) {
    std::lock_guard<std::mutex> lock(registryMutex());
    registry().erase(handle);
}

} // namespace

PluginInstance* findInstance(const void* handle) noexcept {
    std::lock_guard<std::mutex> lock(registryMutex());
    const auto it = registry().find(handle);
    return it == registry().end() ? nullptr : it->second;
}

// ---------------------------------------------------------------------------
// OfxAbortChecker
// ---------------------------------------------------------------------------

bool OfxAbortChecker::aborted() const noexcept {
    HostBridge& bridge = HostBridge::instance();
    const OfxImageEffectSuiteV1* fx = bridge.effect();
    if (fx == nullptr || fx->abort == nullptr || effect_ == nullptr) {
        return false;
    }
    // The suite's abort returns 1 when the user has cancelled. Anything else, including an error
    // status, is treated as "keep going": bailing out on a transport hiccup would be worse than
    // finishing a frame the user then throws away.
    return fx->abort(effect_) != 0;
}

// ---------------------------------------------------------------------------
// PluginInstance
// ---------------------------------------------------------------------------

PluginInstance::PluginInstance(OfxImageEffectHandle effect)
    : effect_(effect), pipeline_(std::make_unique<Pipeline>()), abortChecker_(effect) {
    pipeline_->setAbortChecker(&abortChecker_);

    HostBridge& bridge = HostBridge::instance();
    const OfxImageEffectSuiteV1* fx = bridge.effect();
    if (fx != nullptr && effect_ != nullptr) {
        OfxPropertySetHandle props = nullptr;
        if (fx->getPropertySet(effect_, &props) == kOfxStatOK) {
            effectProps_ = props;
        }
        OfxParamSetHandle ps = nullptr;
        if (fx->getParamSet(effect_, &ps) == kOfxStatOK) {
            paramSet_ = ps;
        }
        resolveClips();
    }

    // failLoudly defaults to off; instanceChanged turns it on from the Diagnostics tab.
    if (paramSet_ != nullptr) {
        const ParamReader p(bridge.params(), paramSet_);
        int loud = 0;
        if (p.getInt(pname::kFailLoudly, 0.0, loud)) {
            failLoudly_ = (loud != 0);
        }
        applyLogSettings(p, 0.0);
    }

    registerInstance(effect_, this);
    AIBGR_LOGI("CreateInstance %p", static_cast<const void*>(effect_));
}

PluginInstance::~PluginInstance() {
    unregisterInstance(effect_);
    AIBGR_LOGI("DestroyInstance %p", static_cast<const void*>(effect_));
}

void PluginInstance::resolveClips() {
    if (clipsResolved_) {
        return;
    }
    HostBridge& bridge = HostBridge::instance();
    const OfxImageEffectSuiteV1* fx = bridge.effect();
    if (fx == nullptr || effect_ == nullptr) {
        return;
    }
    // clipGetHandle leaves the out-params untouched on failure, so initialise before each call.
    OfxImageClipHandle src = nullptr;
    OfxPropertySetHandle srcProps = nullptr;
    if (fx->clipGetHandle(effect_, kOfxImageEffectSimpleSourceClipName, &src, &srcProps) ==
        kOfxStatOK) {
        sourceClip_ = src;
        sourceClipProps_ = srcProps;
    } else {
        AIBGR_LOGE("clipGetHandle('%s') failed", kOfxImageEffectSimpleSourceClipName);
        return; // retry on the next action rather than caching a half-resolved state
    }

    OfxImageClipHandle dst = nullptr;
    OfxPropertySetHandle dstProps = nullptr;
    if (fx->clipGetHandle(effect_, kOfxImageEffectOutputClipName, &dst, &dstProps) == kOfxStatOK) {
        outputClip_ = dst;
        outputClipProps_ = dstProps;
        clipsResolved_ = true;
    } else {
        AIBGR_LOGE("clipGetHandle('%s') failed", kOfxImageEffectOutputClipName);
    }
}

OfxPremult PluginInstance::resolveOutputPremult(const PipelineSettings& settings,
                                                OfxPremult sourcePremult) noexcept {
    switch (settings.outputPremultiplication) {
    case Premultiplication::Premultiplied:
        return OfxPremult::Premultiplied;
    case Premultiplication::Straight:
        return OfxPremult::Unpremultiplied;
    case Premultiplication::Unknown:
        break;
    }
    // "Follow Host": hand back whatever the source clip arrived as, so a premultiplied timeline
    // stays premultiplied and a straight one stays straight. An Opaque source has no alpha to
    // premultiply by, so it is written straight.
    return (sourcePremult == OfxPremult::Premultiplied) ? OfxPremult::Premultiplied
                                                        : OfxPremult::Unpremultiplied;
}

Result PluginInstance::readSettings(double time, PipelineSettings& out, std::string& note) const {
    if (paramSet_ == nullptr) {
        out = PipelineSettings{};
        note = "no parameter set";
        return Result::fail(Status::NotFound, "instance has no parameter set");
    }
    return aibgr::ofx::readSettings(paramSet_, time, out, note);
}

void PluginInstance::publishStatus(const std::string& text) const {
    if (paramSet_ == nullptr) {
        return;
    }
    const ParamReader p(HostBridge::instance().params(), paramSet_);
    // Writing a String parameter is safe here because statusText is declared with
    // EvaluateOnChange = 0, so the host will not respond by re-rendering. This is the only
    // parameter the plugin ever writes during a render.
    if (!p.setString(pname::kStatusText, 0.0, text.c_str())) {
        AIBGR_LOGV("could not publish status text");
    }
}

void PluginInstance::clearCache() {
    pipeline_->releaseBuffers();
    srcFloat_.release();
    dstFloat_.release();
    lastRenderedTime_ = -1e18;
    publishStatus("caches cleared");
    AIBGR_LOGI("Clear Cache pressed on instance %p", static_cast<const void*>(effect_));
}

void PluginInstance::resetTemporalState() {
    // Phase 6 gives this a real stabiliser to reset. Until then it drops the buffers that would
    // have held the recurrent state, which is the same observable effect.
    pipeline_->releaseBuffers();
    lastRenderedTime_ = -1e18;
    publishStatus("temporal state reset");
    AIBGR_LOGI("Reset Temporal pressed on instance %p", static_cast<const void*>(effect_));
}

void PluginInstance::preloadModel() {
    // Phase 3 owns the model manager. Until it exists there is nothing to warm, and claiming
    // otherwise in the status line would be a lie.
    publishStatus("no AI model backend loaded yet (built-in colour key only)");
    AIBGR_LOGI("Preload Model pressed on instance %p; no model backend is linked yet",
               static_cast<const void*>(effect_));
}

void PluginInstance::instanceChanged(const char* paramName, double time) {
    if (paramSet_ == nullptr) {
        return;
    }
    const ParamReader params(HostBridge::instance().params(), paramSet_);

    // The log level can change at any time and must take effect immediately, before anything else
    // in this call logs.
    applyLogSettings(params, time);

    switch (classifyButton(paramName)) {
    case ButtonAction::ResetRefine:
        applyResetRefinement(params, time);
        break;
    case ButtonAction::PreloadModel:
        preloadModel();
        return; // preloadModel already published a status
    case ButtonAction::ClearCache:
        clearCache();
        return;
    case ButtonAction::ResetTemporal:
        resetTemporalState();
        return;
    case ButtonAction::None:
        break;
    }

    PipelineSettings settings;
    std::string note;
    readSettings(time, settings, note);

    // The fallback key's tolerance and softness are pushed into the matte source by
    // Pipeline::process every frame, so there is nothing to reconfigure here.

    int loud = 0;
    if (params.getInt(pname::kFailLoudly, time, loud)) {
        failLoudly_ = (loud != 0);
    }

    publishStatus(note);
    AIBGR_LOGV("InstanceChanged '%s' at t=%g -> %s", paramName ? paramName : "(clip)", time,
               note.c_str());
}

void PluginInstance::beginSequenceRender(OfxPropertySetHandle inArgs) {
    inSequence_ = true;
    lastRenderedTime_ = -1e18;

    FrameContext probe;
    readFrameContext(inArgs, effectProps_, 0.0, probe);
    AIBGR_LOGI("BeginSequenceRender: sequential=%d interactive=%d draft=%d renderScale=%.4g,%.4g",
               probe.sequentialRenderStatus ? 1 : 0, probe.interactiveRender ? 1 : 0,
               probe.draftQuality ? 1 : 0, probe.renderScaleX, probe.renderScaleY);

    if (probe.sequentialRenderStatus) {
        // The host has promised strict frame order, so Phase 6's EMA may accumulate state.
        AIBGR_LOGI("host granted sequential rendering; temporal stabilisation is permitted");
    } else if (HostBridge::instance().caps().sequentialRender == 0) {
        AIBGR_LOGI("host advertises sequentialRender=0; temporal stabilisation will stay off");
    }
}

void PluginInstance::endSequenceRender() {
    inSequence_ = false;
    AIBGR_LOGI("EndSequenceRender");
    // Deliberately NOT releasing buffers: the next sequence should reuse them. The Clear Cache
    // button and PurgeCaches are the explicit ways to give the memory back.
}

void PluginInstance::clipPreferences(OfxPropertySetHandle outArgs) {
    HostBridge& bridge = HostBridge::instance();
    const Props out(bridge.props(), outArgs);
    if (!out.valid()) {
        return;
    }

    // Mirror the source clip so the host never converts behind our back. This is also the safest
    // answer on Resolve Free, where the field guide records only Float being exercised.
    if (sourceClipProps_ != nullptr) {
        const Props src(bridge.props(), sourceClipProps_);
        const std::string depth = src.getString(kOfxImageEffectPropPixelDepth, 0);
        const std::string components = src.getString(kOfxImageEffectPropComponents, 0);
        const std::string premult = src.getString(kOfxImageEffectPropPreMultiplication, 0);

        if (!depth.empty()) {
            out.setString(kOfxImageEffectPropPixelDepth, 0, depth.c_str());
        }
        if (!components.empty()) {
            out.setString(kOfxImageEffectPropComponents, 0, components.c_str());
        }
        if (!premult.empty()) {
            out.setString(kOfxImageEffectPropPreMultiplication, 0, premult.c_str());
            sourcePremult_ = parsePremult(premult);
        }
    }

    // We produce one output frame per input frame at the same time, so the frame rate and the
    // time domain are the source's; the host default is already correct for both.
    AIBGR_LOGV("GetClipPreferences: mirroring source depth/components/premultiplication (%s)",
               premultName(sourcePremult_));
}

bool PluginInstance::isIdentity(double time, std::string& identityClip,
                                double& identityTime) const {
    PipelineSettings settings;
    std::string note;
    if (!readSettings(time, settings, note).ok()) {
        return false; // cannot tell, so do not claim identity
    }

    // Compositor::isPassThrough is the single source of truth for "this frame comes out exactly as
    // it went in". Claiming identity lets the host skip us entirely, which is the cheapest possible
    // disabled state.
    if (isPassThrough(settings)) {
        identityClip = kOfxImageEffectSimpleSourceClipName;
        identityTime = time;
        return true;
    }
    return false;
}

Result PluginInstance::render(double time, OfxPropertySetHandle inArgs) {
    HostBridge& bridge = HostBridge::instance();
    const OfxImageEffectSuiteV1* fx = bridge.effect();
    if (fx == nullptr) {
        return Result::fail(Status::InvalidState, "image effect suite unavailable");
    }
    if (sourceClip_ == nullptr || outputClip_ == nullptr) {
        return Result::fail(Status::NotFound, "Source or Output clip is missing");
    }

    PipelineSettings settings;
    std::string note;
    const Result settingsOk = readSettings(time, settings, note);
    if (!settingsOk.ok()) {
        AIBGR_LOGE("could not read parameters (%s); using built-in defaults",
                   settingsOk.toString().c_str());
    }

    FrameContext ctx;
    if (!readFrameContext(inArgs, effectProps_, time, ctx).ok()) {
        // No render window was reported. Fall back to the output image's own bounds below.
        AIBGR_LOGV("host reported no render window; falling back to the output image bounds");
    }
    ctx.inSequence = inSequence_;

    // Fetch both images. A null region asks the host for its default, which is at least the region
    // of interest we declared - and since we return ReplyDefault for GetRegionsOfInterest that is
    // the whole region of definition. A background remover needs whole-frame context anyway, so
    // asking for a sub-rectangle would only risk an incomplete matte.
    OfxPropertySetHandle srcImgProps = nullptr;
    OfxPropertySetHandle dstImgProps = nullptr;
    const OfxStatus srcStat = fx->clipGetImage(sourceClip_, time, nullptr, &srcImgProps);
    const OfxStatus dstStat = fx->clipGetImage(outputClip_, time, nullptr, &dstImgProps);

    if (dstStat != kOfxStatOK || dstImgProps == nullptr) {
        if (srcImgProps != nullptr) {
            fx->clipReleaseImage(srcImgProps);
        }
        return Result::fail(Status::IoError, "host refused to give us an output image");
    }

    const OfxImageInfo dstInfo = inspectImage(dstImgProps);
    if (!dstInfo.valid()) {
        fx->clipReleaseImage(dstImgProps);
        if (srcImgProps != nullptr) {
            fx->clipReleaseImage(srcImgProps);
        }
        return Result::fail(Status::Unsupported, "output image has an unsupported layout");
    }

    // The render window is what we are REQUIRED to fill; the output bounds are what we are ALLOWED
    // to write. Use the intersection of the two.
    RectI window = ctx.renderWindow;
    if (window.empty()) {
        window = dstInfo.bounds;
    } else {
        window = intersect(window, dstInfo.bounds);
        if (window.empty()) {
            window = dstInfo.bounds;
        }
    }
    ctx.renderWindow = window;
    ctx.bounds = dstInfo.bounds;

    // A source image that cannot be fetched is, per ofxImageEffect.h, to be treated as black and
    // transparent. We still write an output so the host's render is well-formed.
    const bool haveSource = (srcStat == kOfxStatOK && srcImgProps != nullptr);
    OfxImageInfo srcInfo;
    if (haveSource) {
        srcInfo = inspectImage(srcImgProps);
    }

    Result outcome = Result::success();

    if (haveSource && srcInfo.valid()) {
        const bool unpremult = (srcInfo.premultiplication == OfxPremult::Premultiplied);
        const Result read = readImageToFloat(srcInfo, window, srcFloat_, unpremult);
        if (!read.ok()) {
            outcome = read;
        } else {
            const Result proc = pipeline_->process(ctx, settings, srcFloat_, dstFloat_, lastStats_);
            if (!proc.ok()) {
                // D9: never fail a render over a content problem. Pass the source through so the
                // artist sees their original picture rather than a black frame or a crashed host.
                AIBGR_LOGE("pipeline failed (%s); passing the source through unchanged",
                           proc.toString().c_str());
                dstFloat_.resize(srcFloat_.width(), srcFloat_.height());
                std::memcpy(dstFloat_.data(), srcFloat_.data(),
                            srcFloat_.floatCount() * sizeof(float));
                outcome = proc;
            }

            const OfxPremult wantPremult = resolveOutputPremult(settings, srcInfo.premultiplication);
            const Result write = writeFloatToImage(dstInfo, window, dstFloat_, wantPremult);
            if (!write.ok()) {
                outcome = write;
            }
        }
    } else {
        // No usable source. Fill the window with transparent black rather than leaving the host's
        // buffer uninitialised, which is what shows up as random garbage in the viewer.
        AIBGR_LOGE("no usable source image (clipGetImage status %d); writing transparent black",
                   static_cast<int>(srcStat));
        dstFloat_.resize(window.width(), window.height());
        dstFloat_.clearToTransparent();
        writeFloatToImage(dstInfo, window, dstFloat_, OfxPremult::Unpremultiplied);
        outcome = Result::fail(Status::NotFound, "source image unavailable");
    }

    if (srcImgProps != nullptr) {
        fx->clipReleaseImage(srcImgProps);
    }
    fx->clipReleaseImage(dstImgProps);

    // Track frame adjacency for Phase 6. Anything other than exactly +1 frame invalidates the
    // recurrent state, which is why the comparison is exact rather than "close enough".
    const double frameDelta = (lastRenderedTime_ > -1e17) ? (time - lastRenderedTime_) : 0.0;
    lastRenderedTime_ = time;

    if (Log::instance().enabled(LogLevel::Info)) {
        AIBGR_LOGI("Render t=%.4f window=%s matte=%dx%d src=%.1fms refine=%.1fms comp=%.1fms "
                   "total=%.1fms dt=%.4f seq=%d fallback=%d",
                   time, rectToString(window).c_str(), lastStats_.matteWidth, lastStats_.matteHeight,
                   lastStats_.matteSourceMs, lastStats_.refineMs, lastStats_.compositeMs,
                   lastStats_.totalMs, frameDelta, ctx.sequentialRenderStatus ? 1 : 0,
                   lastStats_.usedFallbackKey ? 1 : 0);
    }

    // The Diagnostics status line is only worth the write when something is wrong or when the
    // artist is looking; per-frame writes would spam the host's undo stack on some builds.
    if (!outcome.ok() || !lastStats_.message.empty()) {
        std::string text = outcome.ok() ? lastStats_.message : outcome.toString();
        if (!note.empty()) {
            text += " | ";
            text += note;
        }
        publishStatus(text);
        if (!outcome.ok() && failLoudly_) {
            return outcome;
        }
    }

    return Result::success();
}


// ---------------------------------------------------------------------------
// Describe
// ---------------------------------------------------------------------------

namespace {

/// The models actually available to this build.
///
/// Deliberately empty in Phase 2. Phase 3 replaces this with a real catalogue scan of
/// <pluginDataDir>/models plus a tensor-signature check, because listing a model we cannot run is
/// worse than listing none: the artist picks it and gets a silent pass-through. Until then the
/// Model Selection parameter offers only the built-in colour key, which is exactly what the plugin
/// can honestly do.
std::vector<ModelChoice> discoverModelChoices() {
    AIBGR_LOGI("model catalogue is empty (AI backend lands in Phase 3); offering the built-in "
               "colour key only");
    return {};
}

} // namespace

OfxStatus describeEffect(OfxImageEffectHandle effect) noexcept {
    HostBridge& bridge = HostBridge::instance();
    if (!bridge.ready() || effect == nullptr) {
        return kOfxStatFailed;
    }

    OfxPropertySetHandle props = nullptr;
    if (bridge.effect()->getPropertySet(effect, &props) != kOfxStatOK || props == nullptr) {
        AIBGR_LOGE("Describe: could not get the effect property set");
        return kOfxStatFailed;
    }
    const Props p(bridge.props(), props);

    p.setString(kOfxPropLabel, 0, AIBGR_PLUGIN_LABEL);
    p.setString(kOfxImageEffectPluginPropGrouping, 0, AIBGR_PLUGIN_GROUPING);
    p.setString(kOfxPropPluginDescription, 0,
                "Removes the background of a shot with a locally-run AI segmentation model, "
                "refines the alpha matte to keep hair and fur, and composites the result. Runs "
                "entirely on this machine; nothing is uploaded.");

    // Multiple instances are the normal case: one per timeline node, and the host may keep a
    // descriptor instance alive alongside them.
    p.setInt(kOfxImageEffectPluginPropSingleInstance, 0, 0);

    // Fully safe: every instance owns its Pipeline and its scratch buffers, so instances may
    // render concurrently on different threads. This stays true only while the Phase 3 model
    // manager hands out sessions under a lock - see docs/04-implementation-plan.md, task 3.4.
    p.setString(kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe);

    // Both fields in one pass is fine: nothing here is field-aware.
    p.setInt(kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 0);

    // Source and output always share a depth; GetClipPreferences mirrors the source onto the
    // output. The field guide records Resolve 21 driving this plugin at Float throughout.
    p.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);

    // No custom interact, so no overlay.
    p.setInt(kOfxImageEffectPropSupportsOverlays, 0, 0);

    // Frame varying: with temporal stabilisation on, two renders of the same frame can differ
    // because the recurrent state differs. Declaring this honestly is what stops the host caching
    // a stale frame.
    p.setInt(kOfxImageEffectFrameVarying, 0, 1);

    // Value 2 = "best rendered sequentially, but still correct if not" (ofxImageEffect.h:887).
    // Written as an int directly rather than through a boolean setter, which can only express 0/1.
    p.setInt(kOfxImageEffectInstancePropSequentialRender, 0, 2);

    // Filter is the context Resolve actually puts a timeline effect in; General is declared too so
    // the plugin also appears where a mask input is expected.
    p.setString(kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    p.setString(kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral);

    // All four depths are implemented by plugin/OfxImageAdapter, including the two integer ones
    // that the field guide left untested. Byte and Short are normalised by their integer maximum,
    // NOT treated as floats.
    p.setString(kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
    p.setString(kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
    p.setString(kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthHalf);
    p.setString(kOfxImageEffectPropSupportedPixelDepths, 3, kOfxBitDepthFloat);

    AIBGR_LOGI("Describe: label='%s' grouping='%s' contexts=Filter,General depths=Byte,Short,"
               "Half,Float sequentialRender=2",
               AIBGR_PLUGIN_LABEL, AIBGR_PLUGIN_GROUPING);
    return kOfxStatOK;
}

OfxStatus describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs) noexcept {
    HostBridge& bridge = HostBridge::instance();
    if (!bridge.ready() || effect == nullptr) {
        return kOfxStatFailed;
    }
    const OfxImageEffectSuiteV1* fx = bridge.effect();

    const Props in(bridge.props(), inArgs);
    const std::string context = in.getString(kOfxImageEffectPropContext, 0);
    AIBGR_LOGI("DescribeInContext: context='%s'", context.c_str());

    // Output first, then Source - the order the SDK's own raw-C example uses.
    OfxPropertySetHandle clipProps = nullptr;
    if (fx->clipDefine(effect, kOfxImageEffectOutputClipName, &clipProps) != kOfxStatOK) {
        AIBGR_LOGE("clipDefine('%s') failed", kOfxImageEffectOutputClipName);
        return kOfxStatFailed;
    }
    {
        const Props cp(bridge.props(), clipProps);
        // RGBA only on output: an alpha channel is the whole point of the effect, and an RGB output
        // would silently discard the matte.
        cp.setStringN(kOfxImageEffectPropSupportedComponents, {kOfxImageComponentRGBA});
    }

    clipProps = nullptr;
    if (fx->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &clipProps) != kOfxStatOK) {
        AIBGR_LOGE("clipDefine('%s') failed", kOfxImageEffectSimpleSourceClipName);
        return kOfxStatFailed;
    }
    {
        const Props cp(bridge.props(), clipProps);
        cp.setStringN(kOfxImageEffectPropSupportedComponents,
                      {kOfxImageComponentRGBA, kOfxImageComponentRGB});
        cp.setInt(kOfxImageClipPropIsMask, 0, 0);
        cp.setInt(kOfxImageClipPropOptional, 0, 0);
    }

    OfxParamSetHandle paramSet = nullptr;
    if (fx->getParamSet(effect, &paramSet) != kOfxStatOK || paramSet == nullptr) {
        AIBGR_LOGE("getParamSet failed during DescribeInContext");
        return kOfxStatFailed;
    }

    const Result defined = defineParameters(paramSet, discoverModelChoices());
    if (!defined.ok()) {
        AIBGR_LOGE("defineParameters failed: %s", defined.toString().c_str());
        // Still return OK: a plugin with no parameters is more useful than one Resolve refuses to
        // list, and the log already says what went wrong.
    }
    return kOfxStatOK;
}

// ---------------------------------------------------------------------------
// pluginMain
// ---------------------------------------------------------------------------

OfxStatus pluginMain(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                     OfxPropertySetHandle outArgs) noexcept {
    if (action == nullptr) {
        return kOfxStatErrBadHandle;
    }

    // The action trace is the single most useful diagnostic when a plugin appears to do nothing in
    // Resolve, so it is logged before anything that could throw.
    Log::instance().traceAction(action);

    try {
        const OfxImageEffectHandle effect = const_cast<OfxImageEffectHandle>(
            static_cast<OfxImageEffectHandle>(const_cast<void*>(handle)));
        HostBridge& bridge = HostBridge::instance();

        // --- lifetime -------------------------------------------------------
        if (std::strcmp(action, kOfxActionLoad) == 0) {
            // Open the log FIRST. acquireSuites() is what writes the host capability dump that
            // answers V7, and if the file is not open yet those lines go nowhere - which would make
            // the single most useful diagnostic of the first Resolve run invisible.
            if (ensurePluginDataDir()) {
                Log::instance().setFilePath(defaultLogPath());
                AIBGR_LOGI("=== %s v%s loaded into a new host ===", AIBGR_PLUGIN_LABEL,
                           AIBGR_PLUGIN_VERSION_STRING);
            } else {
                // No writable data directory, so there is no log to read. Continue anyway: the
                // plugin works without one, it is just much harder to diagnose.
            }

            const Result r = bridge.acquireSuites();
            if (!r.ok()) {
                AIBGR_LOGE("Load: %s", r.toString().c_str());
                return kOfxStatFailed;
            }
            AIBGR_LOGI("Load: %s v%s ready", AIBGR_PLUGIN_LABEL, AIBGR_PLUGIN_VERSION_STRING);
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxActionUnload) == 0) {
            AIBGR_LOGI("Unload");
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxActionDescribe) == 0) {
            return describeEffect(effect);
        }

        if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0) {
            return describeInContext(effect, inArgs);
        }

        // CreateInstance and DestroyInstance MUST return kOfxStatOK. The field guide records that
        // answering either with kOfxStatReplyDefault makes Resolve Free report "No parameter is
        // exposed to user" and never call Render at all.
        if (std::strcmp(action, kOfxActionCreateInstance) == 0) {
            if (!bridge.ready()) {
                AIBGR_LOGE("CreateInstance before Load succeeded; acquiring suites now");
                if (!bridge.acquireSuites().ok()) {
                    return kOfxStatFailed;
                }
            }
            // Allocated with new and freed in DestroyInstance. Ownership is deliberately raw: the
            // host, not us, decides the lifetime, and a smart pointer in a C callback boundary only
            // obscures that.
            std::unique_ptr<PluginInstance> instance(new PluginInstance(effect));
            if (findInstance(handle) != instance.get()) {
                AIBGR_LOGE("instance registry did not record the new instance");
                return kOfxStatFailed;
            }
            instance.release();
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxActionDestroyInstance) == 0) {
            if (PluginInstance* instance = findInstance(handle)) {
                delete instance; // the destructor unregisters
            } else {
                AIBGR_LOGE("DestroyInstance for an unregistered handle %p", handle);
            }
            return kOfxStatOK;
        }

        // --- per-instance actions -------------------------------------------
        PluginInstance* instance = findInstance(handle);
        if (instance == nullptr) {
            // Nothing we can do without an instance, and failing here would break hosts that
            // legitimately send instance-scoped actions to a descriptor.
            AIBGR_LOGV("action '%s' arrived with no registered instance for handle %p", action,
                       handle);
            return kOfxStatReplyDefault;
        }

        if (std::strcmp(action, kOfxActionInstanceChanged) == 0) {
            const Props in(bridge.props(), inArgs);
            const std::string name = in.getString(kOfxPropName, 0);
            const double time = in.getDouble(kOfxPropTime, 0, 0.0);
            instance->instanceChanged(name.empty() ? nullptr : name.c_str(), time);
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxActionPurgeCaches) == 0) {
            instance->clearCache();
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxActionSyncPrivateData) == 0) {
            // Nothing is serialised to private data yet. Phase 3 stores the resolved model path
            // there so a reopened project does not have to rediscover it.
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxImageEffectActionGetClipPreferences) == 0) {
            instance->clipPreferences(outArgs);
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxImageEffectActionIsIdentity) == 0) {
            const Props in(bridge.props(), inArgs);
            const double time = in.getDouble(kOfxPropTime, 0, 0.0);
            std::string clip;
            double identityTime = time;
            if (instance->isIdentity(time, clip, identityTime) && !clip.empty()) {
                const Props out(bridge.props(), outArgs);
                // The identity clip is named by a STRING property on outArgs, not a clip handle.
                out.setString(kOfxPropName, 0, clip.c_str());
                out.setDouble(kOfxPropTime, 0, identityTime);
                AIBGR_LOGV("IsIdentity: yes, output == '%s' at t=%g", clip.c_str(), identityTime);
                return kOfxStatOK;
            }
            return kOfxStatReplyDefault;
        }

        if (std::strcmp(action, kOfxImageEffectActionBeginSequenceRender) == 0) {
            instance->beginSequenceRender(inArgs);
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxImageEffectActionRender) == 0) {
            const Props in(bridge.props(), inArgs);
            const double time = in.getDouble(kOfxPropTime, 0, 0.0);
            const Result r = instance->render(time, inArgs);
            if (!r.ok()) {
                // D9: a content failure is logged and reported, not returned, so Resolve keeps
                // playing. Only a genuinely broken render path returns a failure.
                AIBGR_LOGE("Render at t=%g degraded: %s", time, r.toString().c_str());
                bridge.reportError(effect, "AI Background Remover: %s", r.message.c_str());
            }
            return kOfxStatOK;
        }

        if (std::strcmp(action, kOfxImageEffectActionEndSequenceRender) == 0) {
            instance->endSequenceRender();
            return kOfxStatOK;
        }

        // --- actions we deliberately leave to the host -----------------------
        // GetRegionOfDefinition, GetRegionsOfInterest, GetFramesNeeded and GetTimeDomain all have
        // host defaults that are exactly right for a single-input filter: the output RoD is the
        // source RoD, the default RoI is the whole RoD (which is what a segmentation network needs
        // anyway), one input frame is needed, and the time domain is the source's.

    } catch (const std::bad_alloc&) {
        AIBGR_LOGE("action '%s' ran out of memory", action);
        return kOfxStatErrMemory;
    } catch (const std::exception& e) {
        // No C++ exception may ever reach the host (D9). Resolve has no handler for one, so an
        // escaped exception means a crash with no diagnostics.
        AIBGR_LOGE("action '%s' threw std::exception: %s", action, e.what());
        return kOfxStatErrUnknown;
    } catch (...) {
        AIBGR_LOGE("action '%s' threw an unknown exception", action);
        return kOfxStatErrUnknown;
    }

    return kOfxStatReplyDefault;
}

} // namespace ofx
} // namespace aibgr

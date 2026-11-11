// SPDX-License-Identifier: MIT
#include "plugin/HostBridge.h"

#include "util/Log.h"

#include <cstdarg>
#include <cstring>

namespace aibgr {
namespace ofx {

const char* ofxStatusName(OfxStatus status) noexcept {
    switch (status) {
    case kOfxStatOK: return "Ok";
    case kOfxStatFailed: return "Failed";
    case kOfxStatErrFatal: return "ErrFatal";
    case kOfxStatErrUnknown: return "ErrUnknown";
    case kOfxStatErrMissingHostFeature: return "ErrMissingHostFeature";
    case kOfxStatErrUnsupported: return "ErrUnsupported";
    case kOfxStatErrExists: return "ErrExists";
    case kOfxStatErrFormat: return "ErrFormat";
    case kOfxStatErrMemory: return "ErrMemory";
    case kOfxStatErrBadHandle: return "ErrBadHandle";
    case kOfxStatErrBadIndex: return "ErrBadIndex";
    case kOfxStatErrValue: return "ErrValue";
    case kOfxStatReplyYes: return "ReplyYes";
    case kOfxStatReplyNo: return "ReplyNo";
    case kOfxStatReplyDefault: return "ReplyDefault";
#if defined(kOfxStatUnlicensed)
    // Added after OFX 1.4; not every SDK checkout has it. The switch is diagnostic only, so a
    // missing code simply falls through to "Unknown" on an older SDK.
    case kOfxStatUnlicensed: return "Unlicensed";
#endif
#if defined(kOfxStatErrImageFormat)
    case kOfxStatErrImageFormat: return "ErrImageFormat";
#endif
    default: break;
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

void Props::setString(const char* prop, int index, const char* value) const noexcept {
    if (!valid() || prop == nullptr) {
        return;
    }
    const OfxStatus st = suite_->propSetString(handle_, prop, index, value ? value : "");
    if (st != kOfxStatOK) {
        AIBGR_LOGV("propSetString('%s', %d) -> %s", prop, index, ofxStatusName(st));
    }
}

void Props::setStringN(const char* prop, const std::vector<const char*>& values) const noexcept {
    if (!valid() || prop == nullptr || values.empty()) {
        return;
    }
    OfxStatus st = suite_->propSetStringN(handle_, prop, static_cast<int>(values.size()),
                                          values.data());
    if (st != kOfxStatOK) {
        // Some hosts reject the N-form on a property whose dimension is fixed; fall back to
        // indexed writes, which the SDK's own examples use.
        for (std::size_t i = 0; i < values.size(); ++i) {
            st = suite_->propSetString(handle_, prop, static_cast<int>(i),
                                       values[i] ? values[i] : "");
            if (st != kOfxStatOK) {
                AIBGR_LOGV("propSetStringN fallback failed on '%s' index %d -> %s", prop,
                           static_cast<int>(i), ofxStatusName(st));
                break;
            }
        }
    }
}

void Props::setInt(const char* prop, int index, int value) const noexcept {
    if (!valid() || prop == nullptr) {
        return;
    }
    const OfxStatus st = suite_->propSetInt(handle_, prop, index, value);
    if (st != kOfxStatOK) {
        AIBGR_LOGV("propSetInt('%s', %d) -> %s", prop, index, ofxStatusName(st));
    }
}

void Props::setDouble(const char* prop, int index, double value) const noexcept {
    if (!valid() || prop == nullptr) {
        return;
    }
    const OfxStatus st = suite_->propSetDouble(handle_, prop, index, value);
    if (st != kOfxStatOK) {
        AIBGR_LOGV("propSetDouble('%s', %d) -> %s", prop, index, ofxStatusName(st));
    }
}

void Props::setDouble2(const char* prop, double x, double y) const noexcept {
    if (!valid() || prop == nullptr) {
        return;
    }
    const double v[2] = {x, y};
    OfxStatus st = suite_->propSetDoubleN(handle_, prop, 2, v);
    if (st != kOfxStatOK) {
        st = suite_->propSetDouble(handle_, prop, 0, x);
        st = suite_->propSetDouble(handle_, prop, 1, y);
        if (st != kOfxStatOK) {
            AIBGR_LOGV("propSetDouble2('%s') -> %s", prop, ofxStatusName(st));
        }
    }
}

void Props::setPointer(const char* prop, int index, void* value) const noexcept {
    if (!valid() || prop == nullptr) {
        return;
    }
    const OfxStatus st = suite_->propSetPointer(handle_, prop, index, value);
    if (st != kOfxStatOK) {
        AIBGR_LOGV("propSetPointer('%s', %d) -> %s", prop, index, ofxStatusName(st));
    }
}

void Props::reset(const char* prop) const noexcept {
    if (!valid() || prop == nullptr) {
        return;
    }
    const OfxStatus st = suite_->propReset(handle_, prop);
    if (st != kOfxStatOK) {
        AIBGR_LOGV("propReset('%s') -> %s", prop, ofxStatusName(st));
    }
}

int Props::getInt(const char* prop, int index, int fallback) const noexcept {
    if (!valid() || prop == nullptr) {
        return fallback;
    }
    int v = fallback;
    if (suite_->propGetInt(handle_, prop, index, &v) != kOfxStatOK) {
        return fallback;
    }
    return v;
}

double Props::getDouble(const char* prop, int index, double fallback) const noexcept {
    if (!valid() || prop == nullptr) {
        return fallback;
    }
    double v = fallback;
    if (suite_->propGetDouble(handle_, prop, index, &v) != kOfxStatOK) {
        return fallback;
    }
    return v;
}

std::string Props::getString(const char* prop, int index) const noexcept {
    if (!valid() || prop == nullptr) {
        return {};
    }
    char* v = nullptr;
    if (suite_->propGetString(handle_, prop, index, &v) != kOfxStatOK || v == nullptr) {
        return {};
    }
    return std::string(v);
}

void* Props::getPointer(const char* prop, int index) const noexcept {
    if (!valid() || prop == nullptr) {
        return nullptr;
    }
    void* v = nullptr;
    if (suite_->propGetPointer(handle_, prop, index, &v) != kOfxStatOK) {
        return nullptr;
    }
    return v;
}

int Props::dimension(const char* prop) const noexcept {
    if (!valid() || prop == nullptr) {
        return 0;
    }
    int n = 0;
    if (suite_->propGetDimension(handle_, prop, &n) != kOfxStatOK) {
        return 0;
    }
    return n;
}

std::vector<std::string> Props::getStringList(const char* prop) const noexcept {
    std::vector<std::string> out;
    const int n = dimension(prop);
    out.reserve(static_cast<std::size_t>(n > 0 ? n : 0));
    for (int i = 0; i < n; ++i) {
        out.push_back(getString(prop, i));
    }
    return out;
}

OfxRectI Props::getRectI(const char* prop) const noexcept {
    OfxRectI r{0, 0, 0, 0};
    if (!valid() || prop == nullptr) {
        return r;
    }
    if (suite_->propGetIntN(handle_, prop, 4, &r.x1) != kOfxStatOK) {
        return OfxRectI{0, 0, 0, 0};
    }
    return r;
}

OfxPointD Props::getPointD(const char* prop) const noexcept {
    OfxPointD p{1.0, 1.0};
    if (!valid() || prop == nullptr) {
        return p;
    }
    if (suite_->propGetDoubleN(handle_, prop, 2, &p.x) != kOfxStatOK) {
        return OfxPointD{1.0, 1.0};
    }
    return p;
}

// ---------------------------------------------------------------------------
// HostCaps
// ---------------------------------------------------------------------------

bool HostCaps::canUsePages(int tabsNeeded) const noexcept {
    if (maxPages <= 0) {
        return false;
    }
    if (pageRowCount <= 0 || pageColumnCount <= 0) {
        return false;
    }
    return tabsNeeded <= pageColumnCount * pageRowCount;
}

// ---------------------------------------------------------------------------
// HostBridge
// ---------------------------------------------------------------------------

HostBridge& HostBridge::instance() noexcept {
    static HostBridge bridge;
    return bridge;
}

Result HostBridge::acquireSuites() {
    if (host_ == nullptr || host_->fetchSuite == nullptr) {
        return Result::fail(Status::NotFound, "no host pointer was supplied via OfxSetHost");
    }

    auto fetch = [this](const char* name, int version) -> void* {
        // fetchSuite is declared to return const void* (ofxCore.h:69) but every suite pointer it
        // hands back is a table of function pointers we need to call through.
        void* s = const_cast<void*>(host_->fetchSuite(host_->host, name, version));
        if (s == nullptr) {
            AIBGR_LOGE("host does not provide suite %s v%d", name, version);
        }
        return s;
    };

    props_ = static_cast<const OfxPropertySuiteV1*>(fetch(kOfxPropertySuite, 1));
    effect_ = static_cast<const OfxImageEffectSuiteV1*>(fetch(kOfxImageEffectSuite, 1));
    params_ = static_cast<const OfxParameterSuiteV1*>(fetch(kOfxParameterSuite, 1));
    interact_ = static_cast<const OfxInteractSuiteV1*>(host_->fetchSuite(host_->host, kOfxInteractSuite, 1));
    // Optional suites: absence is not fatal.
    memory_ = static_cast<const OfxMemorySuiteV1*>(fetch(kOfxMemorySuite, 1));
    threads_ = static_cast<const OfxMultiThreadSuiteV1*>(fetch(kOfxMultiThreadSuite, 1));
    message_ = static_cast<const OfxMessageSuiteV2*>(host_->fetchSuite(host_->host, kOfxMessageSuite, 2));
    if (message_ == nullptr) {
        message_ = static_cast<const OfxMessageSuiteV2*>(
            host_->fetchSuite(host_->host, kOfxMessageSuite, 1));
        if (message_ != nullptr) {
            AIBGR_LOGI("host provides message suite v1 only; v2 features unavailable");
        }
    }

    if (!ready()) {
        return Result::fail(Status::NotFound,
                            "a required suite (property, image effect or parameter) is missing");
    }

    // --- probe host capabilities -----------------------------------------
    const Props host(props_, host_->host);
    caps_ = HostCaps{};
    caps_.apiMajor = host.getInt(kOfxPropAPIVersion, 0, 1);
    caps_.apiMinor = host.getInt(kOfxPropAPIVersion, 1, 0);
    caps_.hostName = host.getString(kOfxPropName, 0);
    caps_.hostLabel = host.getString(kOfxPropLabel, 0);
    caps_.versionLabel = host.getString(kOfxPropVersionLabel, 0);
    caps_.versionMajor = host.getInt(kOfxPropVersion, 0, 0);
    caps_.versionMinor = host.getInt(kOfxPropVersion, 1, 0);
    caps_.versionMicro = host.getInt(kOfxPropVersion, 2, 0);
    caps_.nativeOrigin = host.getString(kOfxImageEffectHostPropNativeOrigin, 0);

    caps_.isBackground = host.getInt(kOfxImageEffectHostPropIsBackground, 0, 0) != 0;
    caps_.supportsOverlays = host.getInt(kOfxImageEffectPropSupportsOverlays, 0, 0) != 0;
    caps_.supportsMultiResolution =
        host.getInt(kOfxImageEffectPropSupportsMultiResolution, 0, 0) != 0;
    caps_.supportsTiles = host.getInt(kOfxImageEffectPropSupportsTiles, 0, 0) != 0;
    caps_.temporalClipAccess = host.getInt(kOfxImageEffectPropTemporalClipAccess, 0, 0) != 0;
    caps_.supportsMultipleClipDepths =
        host.getInt(kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0) != 0;
    caps_.supportsMultipleClipPARs =
        host.getInt(kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0) != 0;

    caps_.sequentialRender = host.getInt(kOfxImageEffectInstancePropSequentialRender, 0, 0);
#if defined(kOfxParamHostPropSupportsStrChoice)
    caps_.supportsStrChoice = host.getInt(kOfxParamHostPropSupportsStrChoice, 0, 0) != 0;
    caps_.supportsStrChoiceAnimation =
        host.getInt(kOfxParamHostPropSupportsStrChoiceAnimation, 0, 0) != 0;
#else
    // This SDK predates StrChoice (OFX 1.5). Forcing the flag false makes ParamDefs take the
    // index-Choice path, which every host understands.
    caps_.supportsStrChoice = false;
    caps_.supportsStrChoiceAnimation = false;
    AIBGR_LOGI("host: this OpenFX SDK has no kOfxParamTypeStrChoice (pre-1.5); Model Selection "
               "will use an index Choice");
#endif
    caps_.supportsChoiceAnimation = host.getInt(kOfxParamHostPropSupportsChoiceAnimation, 0, 0) != 0;
    caps_.supportsBooleanAnimation = host.getInt(kOfxParamHostPropSupportsBooleanAnimation, 0, 0) != 0;
    caps_.supportsRenderQualityDraft =
        host.getInt(kOfxImageEffectPropRenderQualityDraft, 0, 0) != 0;
    // These two property names are declared in ofxGPURender.h, which this project deliberately
    // does not include: we do not use the OFX GPU render suites, because GPU acceleration happens
    // inside ONNX Runtime instead (docs/03-tradeoffs-and-decisions.md, D8). The capability flags
    // are still worth recording in the log, so the strings are spelled out here with their source
    // line numbers rather than pulling in a header full of CUDA and Metal types.
    static constexpr const char* kCudaRenderSupported =
        "OfxImageEffectPropCudaRenderSupported"; // ofxGPURender.h:590
    static constexpr const char* kCudaStreamSupported =
        "OfxImageEffectPropCudaStreamSupported"; // ofxGPURender.h:628
    caps_.supportsCudaRender = host.getString(kCudaRenderSupported, 0) == "true";
    caps_.supportsCudaStream = host.getString(kCudaStreamSupported, 0) == "true";

    caps_.maxParameters = host.getInt(kOfxParamHostPropMaxParameters, 0, 0);
    caps_.maxPages = host.getInt(kOfxParamHostPropMaxPages, 0, 0);
    caps_.pageRowCount = host.getInt(kOfxParamHostPropPageRowColumnCount, 0, 0);
    caps_.pageColumnCount = host.getInt(kOfxParamHostPropPageRowColumnCount, 1, 0);

    caps_.supportedContexts = host.getStringList(kOfxImageEffectPropSupportedContexts);
    caps_.supportedComponents = host.getStringList(kOfxImageEffectPropSupportedComponents);
    caps_.supportedPixelDepths = host.getStringList(kOfxImageEffectPropSupportedPixelDepths);

    AIBGR_LOGI("host: name='%s' label='%s' version=%d.%d.%d ('%s') OFX API %d.%d",
               caps_.hostName.c_str(), caps_.hostLabel.c_str(), caps_.versionMajor,
               caps_.versionMinor, caps_.versionMicro, caps_.versionLabel.c_str(), caps_.apiMajor,
               caps_.apiMinor);
    AIBGR_LOGI("host caps: sequentialRender=%d strChoice=%d draftQuality=%d tiles=%d "
               "multiRes=%d multiClipDepths=%d multiClipPARs=%d overlays=%d cudaRender=%d "
               "cudaStream=%d origin='%s'",
               caps_.sequentialRender, int(caps_.supportsStrChoice),
               int(caps_.supportsRenderQualityDraft), int(caps_.supportsTiles),
               int(caps_.supportsMultiResolution), int(caps_.supportsMultipleClipDepths),
               int(caps_.supportsMultipleClipPARs), int(caps_.supportsOverlays),
               int(caps_.supportsCudaRender), int(caps_.supportsCudaStream),
               caps_.nativeOrigin.c_str());
    AIBGR_LOGI("host caps: maxParameters=%d maxPages=%d pageGrid=%dx%d", caps_.maxParameters,
               caps_.maxPages, caps_.pageRowCount, caps_.pageColumnCount);

    std::string depths;
    for (const auto& d : caps_.supportedPixelDepths) {
        depths += d;
        depths += ' ';
    }
    std::string contexts;
    for (const auto& c : caps_.supportedContexts) {
        contexts += c;
        contexts += ' ';
    }
    AIBGR_LOGI("host caps: pixelDepths=[%s] contexts=[%s]", depths.c_str(), contexts.c_str());

    return Result::success();
}

void HostBridge::reportError(void* handle, const char* format, ...) const noexcept {
    va_list args;
    va_start(args, format);
    const std::string text = formatStringV(format, args);
    va_end(args);

    AIBGR_LOGE("%s", text.c_str());
    // There is no va_list form in the message suite: ofxMessage.h declares
    // message(handle, type, id, format, ...) and setPersistentMessage(...) only. The text is
    // already formatted above, so it is passed as a single %s argument - which also stops a
    // user-visible string containing a stray '%' from being reinterpreted as a format specifier.
    if (message_ != nullptr && message_->message != nullptr) {
        message_->message(handle, kOfxMessageError, "", "%s", text.c_str());
    }
}

} // namespace ofx
} // namespace aibgr

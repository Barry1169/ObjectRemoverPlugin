// SPDX-License-Identifier: MIT
#pragma once

#include "core/Types.h"
#include "util/Status.h"

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include <string>
#include <vector>

namespace aibgr {
namespace ofx {

/// Human-readable name for an OFX status code, for the action-trace log.
///
/// A numeric status in a log file is nearly useless when debugging inside Resolve, where the log is
/// the only feedback channel.
const char* ofxStatusName(OfxStatus status) noexcept;

/// Thin, failure-tolerant wrapper over an OFX property set.
///
/// Every get returns a caller-supplied default instead of propagating an error, and every set
/// logs on failure. Hosts differ in which optional properties they populate, and a plugin that
/// treats a missing optional property as fatal will not load in more than one host.
class Props {
public:
    Props() = default;
    Props(const OfxPropertySuiteV1* suite, OfxPropertySetHandle handle) noexcept
        : suite_(suite), handle_(handle) {}

    bool valid() const noexcept { return suite_ != nullptr && handle_ != nullptr; }
    OfxPropertySetHandle handle() const noexcept { return handle_; }

    void setString(const char* prop, int index, const char* value) const noexcept;
    void setStringN(const char* prop, const std::vector<const char*>& values) const noexcept;
    void setInt(const char* prop, int index, int value) const noexcept;
    void setDouble(const char* prop, int index, double value) const noexcept;
    void setDouble2(const char* prop, double x, double y) const noexcept;
    void setPointer(const char* prop, int index, void* value) const noexcept;
    void reset(const char* prop) const noexcept;

    int getInt(const char* prop, int index, int fallback) const noexcept;
    double getDouble(const char* prop, int index, double fallback) const noexcept;
    std::string getString(const char* prop, int index) const noexcept;
    void* getPointer(const char* prop, int index) const noexcept;
    int dimension(const char* prop) const noexcept;
    std::vector<std::string> getStringList(const char* prop) const noexcept;

    OfxRectI getRectI(const char* prop) const noexcept;
    OfxPointD getPointD(const char* prop) const noexcept;

private:
    const OfxPropertySuiteV1* suite_ = nullptr;
    OfxPropertySetHandle handle_ = nullptr;
};

/// Everything the host told us about itself, read once on kOfxActionLoad.
///
/// Field names follow OFX::ImageEffectHostDescription in the SDK's own support library
/// (Support/Library/ofxsImageEffect.cpp:1840-1924), which is the authoritative list of host
/// properties. We read the same set by hand so we do not have to link the support library.
struct HostCaps {
    int apiMajor = 1;
    int apiMinor = 0;
    std::string hostName;
    std::string hostLabel;
    std::string versionLabel;
    int versionMajor = 0;
    int versionMinor = 0;
    int versionMicro = 0;
    std::string nativeOrigin;

    bool isBackground = false;
    bool supportsOverlays = false;
    bool supportsMultiResolution = false;
    bool supportsTiles = false;
    bool temporalClipAccess = false;
    bool supportsMultipleClipDepths = false;
    bool supportsMultipleClipPARs = false;

    /// Host's own value for kOfxImageEffectInstancePropSequentialRender: 0 never, 1 always,
    /// 2 sometimes. Temporal stabilisation is only ever enabled when this is non-zero.
    int sequentialRender = 0;

    bool supportsStrChoice = false;
    bool supportsStrChoiceAnimation = false;
    bool supportsChoiceAnimation = false;
    bool supportsBooleanAnimation = false;
    bool supportsRenderQualityDraft = false;

    /// Whether the host offers the OFX GPU render suites. Reported for Phase 9 planning only;
    /// this plugin does not use them (docs/03-tradeoffs-and-decisions.md, D8).
    bool supportsCudaRender = false;
    bool supportsCudaStream = false;

    int maxParameters = 0;
    int maxPages = 0;
    int pageRowCount = 0;
    int pageColumnCount = 0;

    std::vector<std::string> supportedContexts;
    std::vector<std::string> supportedComponents;
    std::vector<std::string> supportedPixelDepths;

    /// True when the host can lay out a Page parameter with at least our tab count.
    bool canUsePages(int tabsNeeded) const noexcept;
};

/// Process-wide access to the host's suites.
///
/// OfxSetHost is only allowed to store the pointer (ofxCore.h forbids calling OFX functions
/// there), so suite acquisition happens on kOfxActionLoad.
class HostBridge {
public:
    static HostBridge& instance() noexcept;

    /// Called from OfxSetHost. Stores the pointer and nothing else.
    void storeHost(const OfxHost* host) noexcept { host_ = host; }

    /// Called on kOfxActionLoad. Fetches suites and probes host capabilities.
    Result acquireSuites();

    bool ready() const noexcept { return props_ != nullptr && effect_ != nullptr && params_ != nullptr; }

    const OfxPropertySuiteV1* props() const noexcept { return props_; }
    const OfxImageEffectSuiteV1* effect() const noexcept { return effect_; }
    const OfxParameterSuiteV1* params() const noexcept { return params_; }
    const OfxMemorySuiteV1* memory() const noexcept { return memory_; }
    const OfxMessageSuiteV2* message() const noexcept { return message_; }
    const OfxMultiThreadSuiteV1* threads() const noexcept { return threads_; }

    const HostCaps& caps() const noexcept { return caps_; }

    Props propsFor(OfxPropertySetHandle handle) const noexcept { return Props(props_, handle); }

    /// Best-effort user-visible message. Falls back to the log if the host has no message suite.
    void reportError(void* handle, const char* format, ...) const noexcept;

private:
    HostBridge() = default;

    const OfxHost* host_ = nullptr;
    const OfxPropertySuiteV1* props_ = nullptr;
    const OfxImageEffectSuiteV1* effect_ = nullptr;
    const OfxParameterSuiteV1* params_ = nullptr;
    const OfxMemorySuiteV1* memory_ = nullptr;
    const OfxMessageSuiteV2* message_ = nullptr;
    const OfxMultiThreadSuiteV1* threads_ = nullptr;
    HostCaps caps_;
};

} // namespace ofx
} // namespace aibgr

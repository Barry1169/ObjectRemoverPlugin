// SPDX-License-Identifier: MIT
#include "plugin/ParamValues.h"

#include "plugin/PluginPaths.h"
#include "util/Log.h"

#include <algorithm>
#include <cstring>

namespace aibgr {
namespace ofx {

namespace {
bool gModelUsesStrChoice = false;
}

void setModelUsesStrChoice(bool value) noexcept { gModelUsesStrChoice = value; }

// ---------------------------------------------------------------------------
// ParamReader
// ---------------------------------------------------------------------------

OfxParamHandle ParamReader::handle(const char* name) const noexcept {
    if (!valid() || name == nullptr) {
        return nullptr;
    }
    OfxParamHandle h = nullptr;
    if (suite_->paramGetHandle(paramSet_, name, &h, nullptr) != kOfxStatOK) {
        return nullptr;
    }
    return h;
}

Props ParamReader::props(const char* name) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return Props();
    }
    OfxPropertySetHandle p = nullptr;
    if (suite_->paramGetPropertySet(h, &p) != kOfxStatOK || p == nullptr) {
        return Props();
    }
    return Props(HostBridge::instance().props(), p);
}

bool ParamReader::getInt(const char* name, double time, int& out) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return false;
    }
    int v = 0;
    if (suite_->paramGetValueAtTime(h, time, &v) != kOfxStatOK) {
        return false;
    }
    out = v;
    return true;
}

bool ParamReader::getDouble(const char* name, double time, double& out) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return false;
    }
    double v = 0.0;
    if (suite_->paramGetValueAtTime(h, time, &v) != kOfxStatOK) {
        return false;
    }
    out = v;
    return true;
}

bool ParamReader::getString(const char* name, double time, std::string& out) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return false;
    }
    char* v = nullptr;
    if (suite_->paramGetValueAtTime(h, time, &v) != kOfxStatOK || v == nullptr) {
        return false;
    }
    out.assign(v);
    return true;
}

bool ParamReader::setInt(const char* name, double time, int value) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return false;
    }
    return suite_->paramSetValueAtTime(h, time, value) == kOfxStatOK;
}

bool ParamReader::setDouble(const char* name, double time, double value) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return false;
    }
    return suite_->paramSetValueAtTime(h, time, value) == kOfxStatOK;
}

bool ParamReader::setString(const char* name, double time, const char* value) const noexcept {
    OfxParamHandle h = handle(name);
    if (h == nullptr) {
        return false;
    }
    return suite_->paramSetValueAtTime(h, time, value ? value : "") == kOfxStatOK;
}

// ---------------------------------------------------------------------------
// Model selection
// ---------------------------------------------------------------------------

bool modelUsesStrChoice() noexcept { return gModelUsesStrChoice; }

ModelId modelIdFromValue(const std::string& value) noexcept {
    // "__fallback__" and anything unrecognised both mean "no AI model".
    if (value.empty() || value == "__fallback__" || value == "none") {
        return ModelId::None;
    }
    if (value == "birefnet-lite") {
        return ModelId::BirefNetLite;
    }
    if (value == "birefnet-general") {
        return ModelId::BirefNetGeneral;
    }
    if (value == "birefnet-portrait") {
        return ModelId::BirefNetPortrait;
    }
    if (value == "small-matte") {
        return ModelId::SmallMatte;
    }
    if (value == "vitmatte") {
        return ModelId::ViTMatte;
    }
    return ModelId::None;
}

const char* modelValueFromId(ModelId id) noexcept {
    switch (id) {
    case ModelId::BirefNetLite: return "birefnet-lite";
    case ModelId::BirefNetGeneral: return "birefnet-general";
    case ModelId::BirefNetPortrait: return "birefnet-portrait";
    case ModelId::SmallMatte: return "small-matte";
    case ModelId::ViTMatte: return "vitmatte";
    case ModelId::Auto: return "auto";
    case ModelId::None: break;
    }
    return "__fallback__";
}

// ---------------------------------------------------------------------------
// readSettings
// ---------------------------------------------------------------------------

Result readSettings(OfxParamSetHandle paramSet, double time, PipelineSettings& out,
                    std::string& statusNote) {
    out = PipelineSettings{}; // documented defaults

    HostBridge& bridge = HostBridge::instance();
    const ParamReader p(bridge.params(), paramSet);
    if (!p.valid()) {
        return Result::fail(Status::NotFound, "parameter suite unavailable");
    }

    int missing = 0;
    std::string modelNote;
    auto wantInt = [&](const char* name, int& dst) {
        if (!p.getInt(name, time, dst)) {
            ++missing;
        }
    };
    auto wantDouble = [&](const char* name, float& dst) {
        double v = 0.0;
        if (p.getDouble(name, time, v)) {
            dst = static_cast<float>(v);
        } else {
            ++missing;
        }
    };
    int enable = 1;
    wantInt(pname::kEnable, enable);
    out.enable = (enable != 0);

    int processingMode = 0;
    wantInt(pname::kProcessingMode, processingMode);
    switch (processingMode) {
    case 1: out.processingMode = ProcessingMode::LargestSubjectOnly; break;
    case 2: out.processingMode = ProcessingMode::AllSubjects; break;
    case 3: out.processingMode = ProcessingMode::InvertRemoveSubject; break;
    default: out.processingMode = ProcessingMode::Automatic; break;
    }

    int quality = 2;
    wantInt(pname::kQuality, quality);
    switch (quality) {
    case 0: out.quality = QualityPreset::Draft256; break;
    case 1: out.quality = QualityPreset::Fast384; break;
    case 2: out.quality = QualityPreset::Balanced512; break;
    case 3: out.quality = QualityPreset::High768; break;
    case 4: out.quality = QualityPreset::Maximum1024; break;
    default: out.quality = QualityPreset::Balanced512; break;
    }

    int colorSpace = 0;
    wantInt(pname::kInputColorSpace, colorSpace);
    switch (colorSpace) {
    case 1: out.inputColorSpace = ColorSpaceAssumption::DisplayReferred; break;
    case 2: out.inputColorSpace = ColorSpaceAssumption::SceneLinear; break;
    case 3: out.inputColorSpace = ColorSpaceAssumption::LogC; break;
    default: out.inputColorSpace = ColorSpaceAssumption::Auto; break;
    }

    wantInt(pname::kDespeckle, out.despecklePx);

    if (modelUsesStrChoice()) {
        std::string value;
        if (p.getString(pname::kModel, time, value)) {
            out.model = modelIdFromValue(value);
        } else {
            ++missing;
        }
    } else {
        int index = 0;
        wantInt(pname::kModel, index);
        // Index 0 is always the built-in fallback key (see defineParameters()). Indices above 0
        // would map into the discovered catalogue, but no catalogue exists until Phase 3, so every
        // index means "no AI model" here. Reporting None is honest; guessing a model would not be.
        out.model = ModelId::None;
        if (index > 0) {
            modelNote = "requested model index " + std::to_string(index) +
                        " but no model catalogue is loaded yet";
        }
    }

    wantDouble(pname::kDetailRefinement, out.detailRefinement);
    wantDouble(pname::kEdgeFeather, out.edgeFeather);
    wantDouble(pname::kEdgeExpansion, out.edgeExpansion);
    wantDouble(pname::kEdgeSmoothing, out.edgeSmoothing);
    wantDouble(pname::kShiftMatte, out.shiftMatte);

    int outputMode = 0;
    wantInt(pname::kOutputMode, outputMode);
    switch (outputMode) {
    case 1: out.outputMode = OutputMode::ForegroundOnly; break;
    case 2: out.outputMode = OutputMode::BackgroundOnly; break;
    case 3: out.outputMode = OutputMode::MatteOnly; break;
    default: out.outputMode = OutputMode::Composite; break;
    }

    wantDouble(pname::kBackgroundTransparency, out.backgroundTransparency);
    int invert = 0;
    wantInt(pname::kMatteInvert, invert);
    out.matteInvert = (invert != 0);

    int premult = 0;
    wantInt(pname::kPremultiplyOutput, premult);
    switch (premult) {
    case 1: out.outputPremultiplication = Premultiplication::Premultiplied; break;
    case 2: out.outputPremultiplication = Premultiplication::Straight; break;
    default: out.outputPremultiplication = Premultiplication::Unknown; break;
    }

    int procRes = 0;
    wantInt(pname::kProcessingResolution, procRes);
    switch (procRes) {
    case 1: out.processingResolution = ProcessingResolutionMode::ForceHalf; break;
    case 2: out.processingResolution = ProcessingResolutionMode::ForceQuarter; break;
    case 3: out.processingResolution = ProcessingResolutionMode::ForceFull; break;
    default: out.processingResolution = ProcessingResolutionMode::FollowHost; break;
    }

    int gpu = 0;
    wantInt(pname::kGpuAcceleration, gpu);
    switch (gpu) {
    case 1: out.gpuBackend = GpuBackend::Cuda; break;
    case 2: out.gpuBackend = GpuBackend::DirectMl; break;
    case 3: out.gpuBackend = GpuBackend::Cpu; break;
    case 4: out.gpuBackend = GpuBackend::Disabled; break;
    default: out.gpuBackend = GpuBackend::Auto; break;
    }

    wantInt(pname::kGpuDeviceId, out.gpuDeviceId);
    wantInt(pname::kGpuMemoryLimit, out.gpuMemoryLimitMB);
    wantInt(pname::kMaxConcurrentInference, out.maxConcurrentInference);
    int cache = 1;
    wantInt(pname::kEnableMatteCache, cache);
    out.enableMatteCache = (cache != 0);
    wantInt(pname::kCacheSizeMB, out.cacheSizeMB);
    wantInt(pname::kThreads, out.intraOpThreads);

    wantDouble(pname::kTemporalStrength, out.temporalStrength);
    wantDouble(pname::kMotionSensitivity, out.motionSensitivity);

    wantDouble(pname::kFallbackKeyTolerance, out.fallbackKeyTolerance);

    out.sanitise();

    statusNote = std::string("model=") + modelIdName(out.model) + " quality=" +
                 qualityName(out.quality) + " output=" +
                 (out.outputMode == OutputMode::Composite      ? "Composite"
                  : out.outputMode == OutputMode::ForegroundOnly ? "ForegroundOnly"
                  : out.outputMode == OutputMode::BackgroundOnly ? "BackgroundOnly"
                                                                 : "MatteOnly");
    if (!modelNote.empty()) {
        statusNote += " [" + modelNote + "]";
    }
    if (missing > 0) {
        statusNote += " (" + std::to_string(missing) + " params unreadable)";
    }

    return Result::success();
}

// ---------------------------------------------------------------------------
// Push buttons and log settings
// ---------------------------------------------------------------------------

ButtonAction classifyButton(const char* paramName) noexcept {
    if (paramName == nullptr) {
        return ButtonAction::None;
    }
    if (std::strcmp(paramName, pname::kResetRefine) == 0) {
        return ButtonAction::ResetRefine;
    }
    if (std::strcmp(paramName, pname::kPreloadModel) == 0) {
        return ButtonAction::PreloadModel;
    }
    if (std::strcmp(paramName, pname::kClearCache) == 0) {
        return ButtonAction::ClearCache;
    }
    if (std::strcmp(paramName, pname::kResetTemporal) == 0) {
        return ButtonAction::ResetTemporal;
    }
    return ButtonAction::None;
}

void applyResetRefinement(const ParamReader& params, double time) noexcept {
    params.setDouble(pname::kDetailRefinement, time, 60.0);
    params.setDouble(pname::kEdgeFeather, time, 1.0);
    params.setDouble(pname::kEdgeExpansion, time, 0.0);
    params.setDouble(pname::kEdgeSmoothing, time, 20.0);
    params.setDouble(pname::kShiftMatte, time, 0.0);
    AIBGR_LOGI("refinement controls reset to defaults");
}

void applyLogSettings(const ParamReader& params, double time) noexcept {
    Log& log = Log::instance();

    // 2 = Info, matching both the inspector control's default and the build-time startup level.
    int level = 2;
    if (!params.getInt(pname::kLogLevel, time, level)) {
        level = 2;
    }
    log.setLevel(static_cast<LogLevel>(std::min(3, std::max(0, level))));

    if (log.filePath().empty()) {
        if (ensurePluginDataDir()) {
            const std::string path = defaultLogPath();
            if (!path.empty()) {
                log.setFilePath(path);
            }
        }
    }
}

} // namespace ofx
} // namespace aibgr

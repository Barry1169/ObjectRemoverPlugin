// SPDX-License-Identifier: MIT
#pragma once

#include "plugin/HostBridge.h"

#include <string>
#include <utility>
#include <vector>

namespace aibgr {
namespace ofx {

/// Parameter script names. These are the identifiers a host stores in a project file, so they are
/// part of the plugin's saved-project ABI and must never be renamed.
namespace pname {

// Background Removal
constexpr const char* kEnable = "enable";
constexpr const char* kProcessingMode = "processingMode";
constexpr const char* kQuality = "quality";
constexpr const char* kModel = "model";
constexpr const char* kInputColorSpace = "inputColorSpace";
constexpr const char* kDespeckle = "despeckle";

// Matte Refinement
constexpr const char* kDetailRefinement = "detailRefinement";
constexpr const char* kEdgeFeather = "edgeFeather";
constexpr const char* kEdgeExpansion = "edgeExpansion";
constexpr const char* kEdgeSmoothing = "edgeSmoothing";
constexpr const char* kShiftMatte = "shiftMatte";
constexpr const char* kBrushMode = "viewerBrushMode";
constexpr const char* kBrushRadius = "viewerBrushRadius";
constexpr const char* kBrushData = "viewerBrushData";
constexpr const char* kBrushUndo = "viewerBrushUndo";
constexpr const char* kBrushClear = "viewerBrushClear";
constexpr const char* kGuidancePath = "guidancePngPath";
constexpr const char* kReloadGuidance = "reloadGuidancePng";
constexpr const char* kResetRefine = "resetRefine";

// Output
constexpr const char* kOutputMode = "outputMode";
constexpr const char* kBackgroundTransparency = "backgroundTransparency";
constexpr const char* kMatteInvert = "matteInvert";
constexpr const char* kPremultiplyOutput = "premultiplyOutput";

// Performance
constexpr const char* kProcessingResolution = "processingResolution";
constexpr const char* kGpuAcceleration = "gpuAcceleration";
constexpr const char* kGpuDeviceId = "gpuDeviceId";
constexpr const char* kGpuMemoryLimit = "gpuMemoryLimitMB";
constexpr const char* kMaxConcurrentInference = "maxConcurrentInference";
constexpr const char* kEnableMatteCache = "enableMatteCache";
constexpr const char* kCacheSizeMB = "cacheSizeMB";
constexpr const char* kPreloadModel = "preloadModel";
constexpr const char* kClearCache = "clearCache";
constexpr const char* kThreads = "threads";

// Temporal (Video)
constexpr const char* kTemporalStrength = "temporalStrength";
constexpr const char* kMotionSensitivity = "motionSensitivity";
constexpr const char* kResetTemporal = "resetTemporal";

// Diagnostics
constexpr const char* kStatusText = "statusText";
constexpr const char* kLogLevel = "logLevel";
constexpr const char* kLogPath = "logPath";
constexpr const char* kFailLoudly = "failLoudly";
constexpr const char* kFallbackKeyTolerance = "fallbackKeyTolerance";
constexpr const char* kFallbackKeySoftness = "fallbackKeySoftness";

// Groups and page
constexpr const char* kGroupRemoval = "grpRemoval";
constexpr const char* kGroupRefine = "grpRefine";
constexpr const char* kGroupOutput = "grpOutput";
constexpr const char* kGroupPerf = "grpPerf";
constexpr const char* kGroupTemporal = "grpTemporal";
constexpr const char* kGroupDiag = "grpDiag";
constexpr const char* kPageMain = "pageMain";

} // namespace pname

/// One entry of the Model Selection list. `value` is what gets stored in a project (a stable
/// identifier, never a display string); `label` is what the artist sees.
struct ModelChoice {
    std::string value;
    std::string label;
};

/// Number of inspector tabs we would like to lay out as a Page parameter.
constexpr int kInspectorTabs = 6;

/// Declare every parameter, in the tab order given by docs/02-architecture.md section 8.
///
/// Called from DescribeInContext. `models` is the catalogue discovered on disk; pass an empty
/// vector and the Model Selection parameter offers only the built-in fallback key.
///
/// Uses kOfxParamTypeStrChoice for the model list when the host advertises support, because a
/// plain index Choice stores an integer in the project file and every index shifts if the user
/// installs or removes a model between sessions. Falls back to an index Choice otherwise, with the
/// caveat logged.
Result defineParameters(OfxParamSetHandle paramSet, const std::vector<ModelChoice>& models);

/// Choice-option tables, shared with ParamValues so the string<->enum mapping lives in one place.
const std::vector<const char*>& processingModeOptions();
const std::vector<const char*>& qualityOptions();
const std::vector<const char*>& colorSpaceOptions();
const std::vector<const char*>& outputModeOptions();
const std::vector<const char*>& premultiplyOptions();
const std::vector<const char*>& processingResolutionOptions();
const std::vector<const char*>& gpuBackendOptions();
const std::vector<const char*>& logLevelOptions();

} // namespace ofx
} // namespace aibgr

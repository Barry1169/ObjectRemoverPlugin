// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include "core/PipelineSettings.h"
#include "plugin/HostBridge.h"
#include "plugin/ParamDefs.h"

#include <string>

namespace aibgr {
namespace ofx {

/// Typed access to a parameter set.
///
/// Every read goes through paramGetValueAtTime. ofxParam.h:1128 is explicit that paramGetValue
/// "should only be called from within a kOfxActionInstanceChanged or interact action and never
/// from the render actions". Getting this wrong produces values that ignore keyframes, which looks
/// like a plugin that randomly ignores animation.
class ParamReader {
public:
    ParamReader(const OfxParameterSuiteV1* suite, OfxParamSetHandle paramSet) noexcept
        : suite_(suite), paramSet_(paramSet) {}

    bool valid() const noexcept { return suite_ != nullptr && paramSet_ != nullptr; }

    OfxParamHandle handle(const char* name) const noexcept;
    Props props(const char* name) const noexcept;

    bool getInt(const char* name, double time, int& out) const noexcept;
    bool getDouble(const char* name, double time, double& out) const noexcept;
    bool getString(const char* name, double time, std::string& out) const noexcept;

    bool setInt(const char* name, double time, int value) const noexcept;
    bool setDouble(const char* name, double time, double value) const noexcept;
    bool setString(const char* name, double time, const char* value) const noexcept;

private:
    const OfxParameterSuiteV1* suite_;
    OfxParamSetHandle paramSet_;
};

/// True when Model Selection was defined as a StrChoice (so its value is read as a string).
/// Recorded by defineParameters().
bool modelUsesStrChoice() noexcept;
void setModelUsesStrChoice(bool value) noexcept;

/// Map a model identifier string to a ModelId. Unknown or absent models map to ModelId::None,
/// which routes the pipeline to the built-in fallback key rather than failing.
ModelId modelIdFromValue(const std::string& value) noexcept;
const char* modelValueFromId(ModelId id) noexcept;

/// Read the whole inspector into a PipelineSettings. `statusNote` receives a short human-readable
/// summary suitable for the Status parameter.
Result readSettings(OfxParamSetHandle paramSet, double time, PipelineSettings& out,
                    std::string& statusNote);

/// Push-button handling.
enum class ButtonAction : uint8_t { None = 0, ResetRefine, PreloadModel, ClearCache, ResetTemporal, ReloadGuidance };

ButtonAction classifyButton(const char* paramName) noexcept;

/// Reset Refinement: writes the documented defaults back at `time`.
void applyResetRefinement(const ParamReader& params, double time) noexcept;

/// Apply the log level chosen in the inspector to the global logger, and set its file path.
/// Called whenever Diagnostics change and once per instance creation.
void applyLogSettings(const ParamReader& params, double time) noexcept;

} // namespace ofx
} // namespace aibgr

// SPDX-License-Identifier: MIT
#include "plugin/ParamDefs.h"

#include "plugin/ParamValues.h"
#include "plugin/PluginPaths.h"
#include "util/Log.h"

#include <cstring>

namespace aibgr {
namespace ofx {

// ---------------------------------------------------------------------------
// Choice option tables (single source of truth shared with ParamValues)
// ---------------------------------------------------------------------------

const std::vector<const char*>& processingModeOptions() {
    static const std::vector<const char*> v = {
        "Automatic",
        "Largest Subject Only",
        "All Subjects",
        "Invert (Remove Subject)",
    };
    return v;
}

const std::vector<const char*>& qualityOptions() {
    static const std::vector<const char*> v = {
        "Draft (256)",
        "Fast (384)",
        "Balanced (512)",
        "High (768)",
        "Maximum (1024)",
    };
    return v;
}

const std::vector<const char*>& colorSpaceOptions() {
    static const std::vector<const char*> v = {
        "Auto (detect)",
        "Display Referred (sRGB / Rec.709)",
        "Scene Linear",
        "Log (ARRI LogC EI800)",
    };
    return v;
}

const std::vector<const char*>& outputModeOptions() {
    static const std::vector<const char*> v = {
        "Composite",
        "Foreground Only",
        "Background Only",
        "Matte Only",
    };
    return v;
}

const std::vector<const char*>& premultiplyOptions() {
    static const std::vector<const char*> v = {
        "Follow Host",
        "Premultiplied",
        "Straight",
    };
    return v;
}

const std::vector<const char*>& processingResolutionOptions() {
    static const std::vector<const char*> v = {
        "Follow Host Render Scale",
        "Force Half",
        "Force Quarter",
        "Force Full",
    };
    return v;
}

const std::vector<const char*>& gpuBackendOptions() {
    static const std::vector<const char*> v = {
        "Auto",
        "CUDA",
        "DirectML",
        "CPU",
        "Disabled",
    };
    return v;
}

const std::vector<const char*>& logLevelOptions() {
    static const std::vector<const char*> v = {
        "Off",
        "Errors",
        "Info",
        "Verbose",
    };
    return v;
}

// ---------------------------------------------------------------------------
// Definition helpers
// ---------------------------------------------------------------------------

namespace {

class Definer {
public:
    Definer(const OfxParameterSuiteV1* suite, OfxParamSetHandle paramSet)
        : suite_(suite), paramSet_(paramSet) {}

    /// Defines a parameter and hands back its PROPERTY SET.
    ///
    /// paramDefine's third out-parameter is a property-set handle, not a parameter handle
    /// (ofxParam.h:1055-1058). Getting that wrong is the easiest way to write a plugin that
    /// compiles against a lenient header and then scribbles over host memory.
    Props define(const char* type, const char* name) {
        if (suite_ == nullptr || paramSet_ == nullptr) {
            return Props();
        }
        OfxPropertySetHandle props = nullptr;
        const OfxStatus st = suite_->paramDefine(paramSet_, type, name, &props);
        if (st != kOfxStatOK || props == nullptr) {
            AIBGR_LOGE("paramDefine('%s', '%s') failed with status %d", type, name,
                       static_cast<int>(st));
            return Props();
        }
        ++count_;
        return Props(HostBridge::instance().props(), props);
    }

    int count() const noexcept { return count_; }

private:
    const OfxParameterSuiteV1* suite_;
    OfxParamSetHandle paramSet_;
    int count_ = 0;
};

void common(const Props& p, const char* label, const char* hint, const char* parent, bool animates,
            bool evaluateOnChange) {
    if (!p.valid()) {
        return;
    }
    if (label != nullptr) {
        p.setString(kOfxPropLabel, 0, label);
    }
    if (hint != nullptr) {
        p.setString(kOfxParamPropHint, 0, hint);
    }
    if (parent != nullptr) {
        p.setString(kOfxParamPropParent, 0, parent);
    }
    p.setInt(kOfxParamPropAnimates, 0, animates ? 1 : 0);
    p.setInt(kOfxParamPropEvaluateOnChange, 0, evaluateOnChange ? 1 : 0);
    p.setInt(kOfxParamPropCanUndo, 0, animates ? 1 : 0);
    p.setInt(kOfxParamPropPersistant, 0, 1);
    p.setInt(kOfxParamPropEnabled, 0, 1);
    p.setInt(kOfxParamPropSecret, 0, 0);
}

void defineGroup(Definer& d, const char* name, const char* label, bool open) {
    const Props p = d.define(kOfxParamTypeGroup, name);
    if (!p.valid()) {
        return;
    }
    p.setString(kOfxPropLabel, 0, label);
    p.setInt(kOfxParamPropGroupOpen, 0, open ? 1 : 0);
    p.setInt(kOfxParamPropEvaluateOnChange, 0, 0);
    // Children attach by NAME through kOfxParamPropParent, so no handle is needed here.
}

void defineBool(Definer& d, const char* name, const char* label, const char* hint,
                const char* parent, bool def) {
    const Props h = d.define(kOfxParamTypeBoolean, name);
    common(h, label, hint, parent, /*animates*/ true, /*evaluateOnChange*/ true);
    if (!h.valid()) {
        return;
    }
    h.setInt(kOfxParamPropDefault, 0, def ? 1 : 0);
}

void defineInt(Definer& d, const char* name, const char* label, const char* hint,
               const char* parent, int def, int lo, int hi) {
    const Props h = d.define(kOfxParamTypeInteger, name);
    common(h, label, hint, parent, true, true);
    if (!h.valid()) {
        return;
    }
    const Props& p = h;
    p.setInt(kOfxParamPropDefault, 0, def);
    p.setInt(kOfxParamPropMin, 0, lo);
    // A display max keeps the slider usable while still allowing typed entry beyond it.
    p.setInt(kOfxParamPropMax, 0, hi);
    p.setInt(kOfxParamPropDisplayMin, 0, lo);
    p.setInt(kOfxParamPropDisplayMax, 0, hi);
}

void defineDouble(Definer& d, const char* name, const char* label, const char* hint,
                  const char* parent, double def, double lo, double hi, double displayLo,
                  double displayHi) {
    const Props h = d.define(kOfxParamTypeDouble, name);
    common(h, label, hint, parent, true, true);
    if (!h.valid()) {
        return;
    }
    const Props& p = h;
    // Plain (unscaled) double: these are pixel amounts and percentages, not canvas coordinates.
    p.setString(kOfxParamPropDoubleType, 0, kOfxParamDoubleTypePlain);
    p.setDouble(kOfxParamPropDefault, 0, def);
    p.setDouble(kOfxParamPropMin, 0, lo);
    p.setDouble(kOfxParamPropMax, 0, hi);
    p.setDouble(kOfxParamPropDisplayMin, 0, displayLo);
    p.setDouble(kOfxParamPropDisplayMax, 0, displayHi);
}

void defineChoice(Definer& d, const char* name, const char* label, const char* hint,
                  const char* parent, const std::vector<const char*>& options, int def) {
    const Props h = d.define(kOfxParamTypeChoice, name);
    common(h, label, hint, parent, /*animates*/ false, /*evaluateOnChange*/ true);
    if (!h.valid()) {
        return;
    }
    const Props& p = h;
    p.setStringN(kOfxParamPropChoiceOption, options);
    p.setInt(kOfxParamPropDefault, 0, def);
}

void defineButton(Definer& d, const char* name, const char* label, const char* hint,
                  const char* parent, bool evaluateOnChange = false) {
    const Props h = d.define(kOfxParamTypePushButton, name);
    common(h, label, hint, parent, /*animates*/ false, evaluateOnChange);
    if (!h.valid()) {
        return;
    }
    h.setInt(kOfxParamPropCanUndo, 0, 0);
}

void defineReadOnlyString(Definer& d, const char* name, const char* label, const char* hint,
                          const char* parent, const std::string& def) {
    const Props h = d.define(kOfxParamTypeString, name);
    common(h, label, hint, parent, /*animates*/ false, /*evaluateOnChange*/ false);
    if (!h.valid()) {
        return;
    }
    const Props& p = h;
    p.setString(kOfxParamPropStringMode, 0, kOfxParamStringIsLabel);
    p.setString(kOfxParamPropDefault, 0, def.c_str());
    // Read-only from the artist's point of view; the plugin may still write it.
    p.setInt(kOfxParamPropEnabled, 0, 0);
}

} // namespace

// ---------------------------------------------------------------------------
// defineParameters
// ---------------------------------------------------------------------------

Result defineParameters(OfxParamSetHandle paramSet, const std::vector<ModelChoice>& models) {
    HostBridge& bridge = HostBridge::instance();
    const OfxParameterSuiteV1* suite = bridge.params();
    if (suite == nullptr) {
        return Result::fail(Status::NotFound, "parameter suite unavailable");
    }

    Definer d(suite, paramSet);
    const HostCaps& caps = bridge.caps();

    // --- Tab 1: Background Removal ----------------------------------------
    defineGroup(d, pname::kGroupRemoval, "Background Removal", /*open*/ true);

    defineBool(d, pname::kEnable, "Enable",
               "Enable background removal and selection. When off, the node reports itself as an identity and the "
               "host skips it entirely, costing nothing.",
               pname::kGroupRemoval, true);

    defineChoice(d, pname::kProcessingMode, "Processing Mode",
                 "What to do with the detected foreground.\n"
                 "Automatic: use the segmentation as produced.\n"
                 "Largest Subject Only: keep only the biggest connected region - the fix for a "
                 "shot where several things were detected.\n"
                 "All Subjects: keep every detected region.\n"
                 "Invert (Remove Subject): remove the subject and keep the background.",
                 pname::kGroupRemoval, processingModeOptions(), 0);

    defineChoice(d, pname::kQuality, "Pipeline Resolution (AI fixed at 320)",
                 "Resolution used before the matte stage. U2NetP always runs at 320 by 320 on CPU; "
                 "this setting does not change the model input size.",
                 pname::kGroupRemoval, qualityOptions(), 2);

    defineChoice(d, pname::kInputColorSpace, "Input Color Space",
                 "What the incoming pixels mean. This affects ONLY the image handed to the "
                 "segmentation network - the composited output is always taken from the original "
                 "untouched pixels, so this control cannot introduce a colour shift. Getting it "
                 "wrong degrades the matte, not your colours.\n"
                 "Auto picks between Display Referred and Scene Linear from mean luminance.",
                 pname::kGroupRemoval, colorSpaceOptions(), 0);

    defineInt(d, pname::kDespeckle, "Despeckle (not implemented yet)",
              "Discards matte islands smaller than this many pixels before refinement. Removes "
              "the speckle a segmentation network sometimes leaves in a busy background.",
              pname::kGroupRemoval, 8, 0, 1000);

    // Model Selection. StrChoice when the host supports it (the value stored in a project is then
    // the model's stable identifier, so adding or removing a model between sessions cannot
    // silently re-point a saved project at a different model). Index Choice otherwise.
    std::vector<const char*> modelLabels;
    std::vector<const char*> modelValues;
    modelLabels.reserve(models.size() + 1);
    modelValues.reserve(models.size() + 1);
    static const char* const kFallbackLabel = "No AI model (built-in colour key)";
    static const char* const kFallbackValue = "__fallback__";
    modelLabels.push_back(kFallbackLabel);
    modelValues.push_back(kFallbackValue);
    for (const auto& m : models) {
        modelLabels.push_back(m.label.c_str());
        modelValues.push_back(m.value.c_str());
    }

    // kOfxParamTypeStrChoice is OFX 1.5. On an older SDK HostBridge has already forced
    // caps.supportsStrChoice to false, and the #if keeps the branch out of the compile so the
    // index-Choice path below is all that is built.
#if defined(kOfxParamTypeStrChoice)
    if (caps.supportsStrChoice) {
        setModelUsesStrChoice(true);
        const Props h = d.define(kOfxParamTypeStrChoice, pname::kModel);
        common(h, "Automatic Model (legacy)",
               "Legacy automatic model; not used by AI box selection. The list is built from the models actually "
               "present on disk, so anything missing simply does not appear.",
               pname::kGroupRemoval, /*animates*/ false, /*evaluateOnChange*/ true);
        if (h.valid()) {
            const Props& p = h;
            p.setStringN(kOfxParamPropChoiceOption, modelLabels);
            p.setStringN(kOfxParamPropChoiceEnum, modelValues);
            p.setString(kOfxParamPropDefault, 0, models.empty() ? kFallbackValue : modelValues[1]);
        }
    } else
#endif
    {
        setModelUsesStrChoice(false);
        AIBGR_LOGI("StrChoice unavailable; Model Selection uses fixed indices (0=key, 1=U2NetP)");
        defineChoice(d, pname::kModel, "Automatic Model (legacy)",
                     "Legacy automatic model; not used by AI box selection. The list is built from the models actually "
                     "present on disk. Existing colour-key choices remain at index zero.",
                     pname::kGroupRemoval, modelLabels, models.empty() ? 0 : 1);
    }

    // These controls apply to the colour-key fallback only, not to the AI model.
    defineDouble(d, pname::kFallbackKeyTolerance, "Background Colour Tolerance",
                 "Controls how close a pixel must be to the estimated border colour to be removed. "
                 "Higher removes more similar colours; lower preserves more. Colour key only, not AI.",
                 pname::kGroupRemoval, 0.25, 0.0, 1.0, 0.0, 1.0);
    defineDouble(d, pname::kFallbackKeySoftness, "Background Colour Softness",
                 "Softens the colour-key transition; only affects the built-in colour key.",
                 pname::kGroupRemoval, 0.08, 0.0, 1.0, 0.0, 1.0);

    // --- Tab 2: Matte Refinement ------------------------------------------
    defineGroup(d, pname::kGroupRefine, "Matte Refinement", /*open*/ true);

    defineDouble(d, pname::kDetailRefinement, "Refine Edges (not implemented yet)",
                 "Transfers fine detail (hair, fur, foliage) from the full-resolution frame back "
                 "into the matte using a guided filter. This is what stops a matte computed at 512 "
                 "from looking soft at 4K. 0 = plain bicubic upscale.",
                 pname::kGroupRefine, 60.0, 0.0, 100.0, 0.0, 100.0);

    defineDouble(d, pname::kEdgeFeather, "Edge Feather (not implemented yet)",
                 "Softness of the matte transition, in source pixels.", pname::kGroupRefine, 1.0,
                 0.0, 100.0, 0.0, 20.0);

    defineDouble(d, pname::kEdgeExpansion, "Edge Expansion (not implemented yet)",
                 "Grows or shrinks the foreground selection, in source pixels. Negative chokes "
                 "the matte inwards to kill a fringe; positive grows it outwards to recover an "
                 "edge the model clipped.",
                 pname::kGroupRefine, 0.0, -100.0, 100.0, -20.0, 20.0);

    defineDouble(d, pname::kEdgeSmoothing, "Foreground Smoothing (not implemented yet)",
                 "Edge-aware smoothing of the matte boundary. Reduces jaggies and rough edges "
                 "without eroding hair the way a plain blur would.",
                 pname::kGroupRefine, 20.0, 0.0, 100.0, 0.0, 100.0);

    defineDouble(d, pname::kShiftMatte, "Matte Bias",
                 "Shifts the whole matte up or down. A small positive value recovers a subject "
                 "the model clipped; a small negative value kills a bright fringe.",
                 pname::kGroupRefine, 0.0, -0.5, 0.5, -0.2, 0.2);

    defineChoice(d, pname::kSelectionMode, "Viewer Selection Method (experimental)",
                 "Ready leaves the image untouched until you add an AI box or point prompt. The other modes explicitly enable the legacy automatic matte or the AI prompt selector. AI prompts require the optional EfficientSAM model.",
                 pname::kGroupRefine, {"Automatic + manual corrections", "Automatic + guided selection", "AI box/point selection (experimental)", "Ready (no automatic removal)"}, 3);
    defineBool(d, pname::kBoxDetailRecovery, "AI Prompts: Recover More Detail (experimental)",
               "Tries an additional overlapping AI mask for add boxes to recover missing foliage and branches. It may also retain sky; turn off if the result is worse. Remove prompts and point prompts are unchanged.",
               pname::kGroupRefine, false);
    defineChoice(d, pname::kPromptRecompute, "AI Prompts: Recompute",
                 "Video changes every frame, so the AI has to run again on each one; on CPU that is slow. "
                 "Every frame follows motion exactly. Reuse while playing keeps the last selection during interactive playback and recomputes when playback stops or a prompt changes. Once per prompt change never follows motion and suits stills or locked-off shots.",
                 pname::kGroupRefine, {"Every frame (accurate)", "Reuse while playing (smooth)",
                                       "Once per prompt change (stills)"}, 0);
    defineChoice(d, pname::kGuidedBrushBehavior, "Guided Brush Behaviour",
                 "Direct corrections preserve the original behaviour. Samples only uses strokes as selection guidance without a full-resolution painted patch. Only affects Guided selection; both foreground and background samples are needed.",
                 pname::kGroupRefine, {"Direct corrections + samples", "Samples only (experimental)"}, 0);
    defineChoice(d, pname::kBrushMode, "Viewer Brush (experimental)",
                 "Enable Open FX Overlay, then draw an outline or paint Keep/Remove markings. "
                 "Keep/Remove paint or guide the matte. AI add/remove box selects predicted objects inside or near rough rectangles. AI add/remove point selects the object under a single click and unites it with, or subtracts it from, the other prompts. The AI tools require the optional model.",
                 pname::kGroupRefine, {"Off", "Keep (green)", "Remove (red)", "Outline (guided)", "AI add box", "AI remove box", "AI add point", "AI remove point"}, 4);
    defineDouble(d, pname::kBrushRadius, "Viewer Brush Radius",
                 "Brush radius in full-resolution image pixels.", pname::kGroupRefine,
                 25.0, 1.0, 200.0, 1.0, 150.0);
    defineButton(d, pname::kBrushUndo, "Undo Last Brush Stroke", "Remove the last painted stroke.", pname::kGroupRefine, true);
    defineButton(d, pname::kBrushClear, "Clear Viewer Brush Strokes", "Remove all painted strokes.", pname::kGroupRefine, true);
    const Props brushData = d.define(kOfxParamTypeString, pname::kBrushData);
    common(brushData, "Saved viewer brush strokes", nullptr, pname::kGroupRefine, false, true);
    if (brushData.valid()) {
        brushData.setString(kOfxParamPropStringMode, 0, kOfxParamStringIsSingleLine);
        brushData.setString(kOfxParamPropDefault, 0, "");
        brushData.setInt(kOfxParamPropSecret, 0, 1);
    }

    defineButton(d, pname::kResetRefine, "Reset Refinement",
                 "Restores the refinement controls to their defaults.", pname::kGroupRefine);

    const Props guidance = d.define(kOfxParamTypeString, pname::kGuidancePath);
    common(guidance, "Guidance PNG (green keep, red remove)",
           "Optional transparent PNG exactly the render frame size. Paint opaque green over "
           "areas to keep and opaque red over areas to remove; transparent areas use the "
           "AI or colour-key matte. No automatic edge detection. For stills or locked-off shots.",
           pname::kGroupRefine, false, true);
    if (guidance.valid()) {
        guidance.setString(kOfxParamPropStringMode, 0, kOfxParamStringIsFilePath);
        guidance.setString(kOfxParamPropDefault, 0, "");
    }
    defineButton(d, pname::kReloadGuidance, "Reload Guidance PNG",
                 "Reload the guidance file on the next render after editing it externally.",
                 pname::kGroupRefine, true);

    // --- Tab 3: Output -----------------------------------------------------
    defineGroup(d, pname::kGroupOutput, "Output", /*open*/ false);

    defineChoice(d, pname::kOutputMode, "Output Mode",
                 "Composite: subject over the (optionally retained) original background.\n"
                 "Foreground Only: the isolated subject, background fully transparent.\n"
                 "Background Only: just the removed background, with a hole where the subject "
                 "was.\n"
                 "Matte Only: the alpha as a greyscale image.",
                 pname::kGroupOutput, outputModeOptions(), 0);

    defineDouble(d, pname::kBackgroundTransparency, "Background Transparency",
                 "How transparent the removed background becomes. 1.0 (the default) removes it "
                 "completely; 0.0 leaves the frame untouched, which makes Composite identical to "
                 "no effect at all. Lets the original background be faded back in behind the "
                 "subject. Has no effect on Foreground Only, Background Only or Matte Only.",
                 pname::kGroupOutput, 1.0, 0.0, 1.0, 0.0, 1.0);

    defineBool(d, pname::kMatteInvert, "Invert Matte", "Shows or removes the opposite region.",
               pname::kGroupOutput, false);

    defineChoice(d, pname::kPremultiplyOutput, "Output Premultiplication",
                 "Normally leave this on Follow Host. Use Premultiplied or Straight to fix a dark "
                 "halo or a bright fringe if a particular host page disagrees about "
                 "premultiplication.",
                 pname::kGroupOutput, premultiplyOptions(), 0);

    // --- Tab 4: Performance -------------------------------------------------
    defineGroup(d, pname::kGroupPerf, "Performance", /*open*/ false);

    defineChoice(d, pname::kProcessingResolution, "Processing Resolution",
                 "Caps the resolution the segmentation runs at, independently of Segmentation "
                 "Quality. Force Half and Force Quarter trade accuracy for speed; Force Full runs "
                 "at the frame's own resolution and is expensive.",
                 pname::kGroupPerf, processingResolutionOptions(), 0);

    defineChoice(d, pname::kGpuAcceleration, "GPU Acceleration (not implemented yet)",
                 "Not implemented yet. U2NetP currently runs on CPU regardless of this setting.",
                 pname::kGroupPerf, gpuBackendOptions(), 0);

    defineInt(d, pname::kGpuDeviceId, "GPU Device (not implemented yet)", "Adapter index, for multi-GPU machines.",
              pname::kGroupPerf, 0, 0, 7);

    defineInt(d, pname::kGpuMemoryLimit, "GPU Memory Limit (not implemented yet)",
              "Hard cap on the inference arena. On a 4 GB card that is also driving the desktop "
              "and the host, 2048 is a sensible cap. 0 means no cap.",
              pname::kGroupPerf, 2048, 0, 65536);

    defineInt(d, pname::kMaxConcurrentInference, "Max Concurrent Inferences (not implemented yet)",
              "How many frames may run the model at once. Above 1 needs VRAM you probably do not "
              "have on a 4 GB card.",
              pname::kGroupPerf, 1, 1, 4);

    defineBool(d, pname::kEnableMatteCache, "Enable Matte Cache (not implemented yet)",
               "Caches raw mattes so scrubbing the refinement controls does not re-run the model.",
               pname::kGroupPerf, true);

    defineInt(d, pname::kCacheSizeMB, "Matte Cache Size (not implemented yet)", "Memory budget for the matte cache.",
              pname::kGroupPerf, 512, 0, 8192);

    defineInt(d, pname::kThreads, "CPU Threads",
              "Intra-op threads for the ONNX Runtime CPU sessions. 0 = automatic: every core, capped "
              "at 8 so Resolve keeps some CPU. Changing this reloads the model on the next render.",
              pname::kGroupPerf, 0, 0, 64);

    defineButton(d, pname::kPreloadModel, "Retry AI Load",
                 "Retry loading the AI runtime and model on the next render after an earlier error.",
                 pname::kGroupPerf);

    defineButton(d, pname::kClearCache, "Clear Cache",
                 "Releases reusable image buffers. The AI session stays loaded.",
                 pname::kGroupPerf);

    // --- Tab 5: Temporal (Video) --------------------------------------------
    defineGroup(d, pname::kGroupTemporal, "Temporal (Video)", /*open*/ false);

    defineDouble(d, pname::kTemporalStrength, "Temporal Stability",
                 "Blends each frame's matte towards the previous frame's, motion-aware, to reduce "
                 "flicker on video. 0 disables it. Only active while the host reports it is "
                 "rendering in strict frame order; scrubbing always produces the correct "
                 "single-frame result. Higher values are more stable but can ghost on fast motion.",
                 pname::kGroupTemporal, 0.35, 0.0, 1.0, 0.0, 1.0);

    defineDouble(d, pname::kMotionSensitivity, "Motion Sensitivity",
                 "How quickly the stabiliser releases when the image moves. Raise it if fast motion "
                 "smears; lower it if flicker survives.",
                 pname::kGroupTemporal, 0.5, 0.0, 1.0, 0.0, 1.0);

    defineButton(d, pname::kResetTemporal, "Reset Temporal State",
                 "Clears the recurrent state after a scrub or a hard cut.", pname::kGroupTemporal);

    // --- Tab 6: Diagnostics ---------------------------------------------------
    defineGroup(d, pname::kGroupDiag, "Diagnostics", /*open*/ false);

    // Default Info, not Errors: the host capability dump and the per-frame render summary are both
    // logged at Info, and they are the first things to read when the plugin misbehaves in Resolve.
    defineChoice(d, pname::kLogLevel, "Log Level",
                 "Info (the default) records what the host advertises at load and one line per "
                 "render. Verbose additionally traces every host action and every property that "
                 "failed to set, which is the fastest way to find out what the host is actually "
                 "doing. Off writes nothing. The log path is shown below.",
                 pname::kGroupDiag, logLevelOptions(), 2);

    defineReadOnlyString(d, pname::kStatusText, "Status",
                         "See the log for model loading and per-frame diagnostics.",
                         pname::kGroupDiag, "See log for AI status (CPU only)");

    defineReadOnlyString(d, pname::kLogPath, "Log File",
                         "Log path selected on plugin load. If unavailable, see the ProgramData or local cache log.",
                         pname::kGroupDiag, defaultLogPath());

    defineBool(d, pname::kFailLoudly, "Fail Loudly If Model Missing (not implemented yet)",
               "Development aid. Off (the default) means a missing model or runtime degrades to "
               "the built-in fallback and logs why. On means the node reports a hard failure so "
               "the problem is impossible to miss.",
               pname::kGroupDiag, false);

    AIBGR_LOGI("defined %d parameters", d.count());

    // --- Page layout ----------------------------------------------------------
    // Only if the host can actually lay out six tabs. Otherwise the groups stand alone, which
    // every host understands.
    if (caps.canUsePages(kInspectorTabs)) {
        const Props page = d.define(kOfxParamTypePage, pname::kPageMain);
        if (page.valid()) {
            const Props& p = page;
            p.setString(kOfxPropLabel, 0, "AI Background Remover");
            p.setInt(kOfxParamPropAnimates, 0, 0);
            p.setInt(kOfxParamPropEvaluateOnChange, 0, 0);

            // One group per column of the first row. Hosts that report a smaller grid were
            // already filtered out by canUsePages().
            const int cols = caps.pageColumnCount > 0 ? caps.pageColumnCount : 1;
            const char* tabs[kInspectorTabs] = {
                pname::kGroupRemoval, pname::kGroupRefine,   pname::kGroupOutput,
                pname::kGroupPerf,    pname::kGroupTemporal, pname::kGroupDiag,
            };
            for (int i = 0; i < kInspectorTabs && i < cols; ++i) {
                p.setString(kOfxParamPropPageChild, i, tabs[i]);
            }
        }
    } else {
        AIBGR_LOGI("host reports maxPages=%d pageGrid=%dx%d; skipping the Page parameter and "
                   "showing the six groups directly",
                   caps.maxPages, caps.pageRowCount, caps.pageColumnCount);
    }

    return Result::success();
}

} // namespace ofx
} // namespace aibgr

// SPDX-License-Identifier: MIT
#include "plugin/ParamDefs.h"

#include "plugin/ParamValues.h"
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
                  const char* parent) {
    const Props h = d.define(kOfxParamTypePushButton, name);
    common(h, label, hint, parent, /*animates*/ false, /*evaluateOnChange*/ false);
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
               "Auto Remove Background. When off, the node reports itself as an identity and the "
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

    defineChoice(d, pname::kQuality, "Segmentation Quality",
                 "The resolution the segmentation runs at, on the long edge. Higher is more "
                 "accurate on fine detail but costs VRAM and time. On a 4 GB GPU, Balanced (512) "
                 "is the recommended starting point; Maximum (1024) is intended for stills.",
                 pname::kGroupRemoval, qualityOptions(), 2);

    defineChoice(d, pname::kInputColorSpace, "Input Color Space",
                 "What the incoming pixels mean. This affects ONLY the image handed to the "
                 "segmentation network - the composited output is always taken from the original "
                 "untouched pixels, so this control cannot introduce a colour shift. Getting it "
                 "wrong degrades the matte, not your colours.\n"
                 "Auto picks between Display Referred and Scene Linear from mean luminance.",
                 pname::kGroupRemoval, colorSpaceOptions(), 0);

    defineInt(d, pname::kDespeckle, "Despeckle",
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

    if (caps.supportsStrChoice) {
        setModelUsesStrChoice(true);
        const Props h = d.define(kOfxParamTypeStrChoice, pname::kModel);
        common(h, "Model Selection",
               "Which segmentation model to run. The list is built from the models actually "
               "present on disk, so anything missing simply does not appear.",
               pname::kGroupRemoval, /*animates*/ false, /*evaluateOnChange*/ true);
        if (h.valid()) {
            const Props& p = h;
            p.setStringN(kOfxParamPropChoiceOption, modelLabels);
            p.setString(kOfxParamPropDefault, 0, kFallbackValue);
        }
    } else {
        setModelUsesStrChoice(false);
        AIBGR_LOGI("host does not support StrChoice; Model Selection uses an index Choice. "
                   "Adding or removing a model file may re-point saved projects.");
        defineChoice(d, pname::kModel, "Model Selection",
                     "Which segmentation model to run. The list is built from the models actually "
                     "present on disk. NOTE: this host stores choices by index, so adding or "
                     "removing a model file between sessions can re-point a saved project.",
                     pname::kGroupRemoval, modelLabels, 0);
    }

    // --- Tab 2: Matte Refinement ------------------------------------------
    defineGroup(d, pname::kGroupRefine, "Matte Refinement", /*open*/ true);

    defineDouble(d, pname::kDetailRefinement, "Refine Edges",
                 "Transfers fine detail (hair, fur, foliage) from the full-resolution frame back "
                 "into the matte using a guided filter. This is what stops a matte computed at 512 "
                 "from looking soft at 4K. 0 = plain bicubic upscale.",
                 pname::kGroupRefine, 60.0, 0.0, 100.0, 0.0, 100.0);

    defineDouble(d, pname::kEdgeFeather, "Edge Feather",
                 "Softness of the matte transition, in source pixels.", pname::kGroupRefine, 1.0,
                 0.0, 100.0, 0.0, 20.0);

    defineDouble(d, pname::kEdgeExpansion, "Edge Expansion",
                 "Grows or shrinks the foreground selection, in source pixels. Negative chokes "
                 "the matte inwards to kill a fringe; positive grows it outwards to recover an "
                 "edge the model clipped.",
                 pname::kGroupRefine, 0.0, -100.0, 100.0, -20.0, 20.0);

    defineDouble(d, pname::kEdgeSmoothing, "Foreground Smoothing",
                 "Edge-aware smoothing of the matte boundary. Reduces jaggies and rough edges "
                 "without eroding hair the way a plain blur would.",
                 pname::kGroupRefine, 20.0, 0.0, 100.0, 0.0, 100.0);

    defineDouble(d, pname::kShiftMatte, "Matte Bias",
                 "Shifts the whole matte up or down. A small positive value recovers a subject "
                 "the model clipped; a small negative value kills a bright fringe.",
                 pname::kGroupRefine, 0.0, -0.5, 0.5, -0.2, 0.2);

    defineButton(d, pname::kResetRefine, "Reset Refinement",
                 "Restores the refinement controls to their defaults.", pname::kGroupRefine);

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

    defineChoice(d, pname::kGpuAcceleration, "GPU Acceleration",
                 "Auto probes CUDA, then DirectML, then CPU, and reports what it picked under "
                 "Diagnostics. Whichever is chosen depends on which onnxruntime.dll is installed "
                 "next to the plugin - the same .ofx works with any of them.",
                 pname::kGroupPerf, gpuBackendOptions(), 0);

    defineInt(d, pname::kGpuDeviceId, "GPU Device", "Adapter index, for multi-GPU machines.",
              pname::kGroupPerf, 0, 0, 7);

    defineInt(d, pname::kGpuMemoryLimit, "GPU Memory Limit (MB)",
              "Hard cap on the inference arena. On a 4 GB card that is also driving the desktop "
              "and the host, 2048 is a sensible cap. 0 means no cap.",
              pname::kGroupPerf, 2048, 0, 65536);

    defineInt(d, pname::kMaxConcurrentInference, "Max Concurrent Inferences",
              "How many frames may run the model at once. Above 1 needs VRAM you probably do not "
              "have on a 4 GB card.",
              pname::kGroupPerf, 1, 1, 4);

    defineBool(d, pname::kEnableMatteCache, "Enable Matte Cache",
               "Caches raw mattes so scrubbing the refinement controls does not re-run the model.",
               pname::kGroupPerf, true);

    defineInt(d, pname::kCacheSizeMB, "Matte Cache Size (MB)", "Memory budget for the matte cache.",
              pname::kGroupPerf, 512, 0, 8192);

    defineInt(d, pname::kThreads, "CPU Threads",
              "Intra-op thread count for the CPU execution provider. 0 lets the runtime choose.",
              pname::kGroupPerf, 0, 0, 64);

    defineButton(d, pname::kPreloadModel, "Preload Model",
                 "Loads the model now instead of on the first rendered frame, so the first frame "
                 "is not slow.",
                 pname::kGroupPerf);

    defineButton(d, pname::kClearCache, "Clear Cache",
                 "Drops cached mattes and releases the loaded model, returning VRAM to the host "
                 "immediately.",
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
                         "What the plugin last did: which model, which execution provider, the "
                         "matte resolution and where the frame's time went.",
                         pname::kGroupDiag, "not rendered yet");

    defineReadOnlyString(d, pname::kLogPath, "Log File",
                         "Where the diagnostic log is written. Empty means file logging is off.",
                         pname::kGroupDiag, "");

    defineDouble(d, pname::kFallbackKeyTolerance, "Fallback Key Tolerance",
                 "Used only when no AI model is available: the built-in border-colour key's "
                 "tolerance. Raise it if too much background survives, lower it if the subject is "
                 "being eaten.",
                 pname::kGroupDiag, 0.25, 0.0, 1.0, 0.0, 1.0);

    defineBool(d, pname::kFailLoudly, "Fail Loudly If Model Missing",
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

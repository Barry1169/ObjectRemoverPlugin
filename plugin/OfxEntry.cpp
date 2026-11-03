// SPDX-License-Identifier: MIT
//
// The DLL's exported surface. This is the ONLY translation unit that defines an exported symbol,
// and the only place the OFX plugin ABI is written down.
//
// Verify after building (docs/05-build-install-test-windows.md):
//
//   dumpbin /exports AiBgRemover.ofx
//
// The two names must appear PLAIN - no leading underscore, no @n suffix. A decorated name means
// the definition did not match the extern "C" declaration in ofxCore.h, and Resolve will load the
// binary and then report that it contains no plugins.
//
//   dumpbin /dependents AiBgRemover.ofx
//
// With the static CRT this must list KERNEL32.dll and nothing else. Anything else is a runtime DLL
// that has to ship inside the bundle, and a version mismatch there is the classic "works on my
// machine, blank in Resolve" failure.

#include <ofxCore.h>
#include <ofxImageEffect.h>

#include "aibgr/PluginIdentity.h"
#include "plugin/BgRemoverPlugin.h"
#include "plugin/HostBridge.h"
#include "util/Log.h"

// ---------------------------------------------------------------------------
// Export linkage. Deliberately our own macro, NOT the SDK's OfxExport.
//
// ofxCore.h defines OfxExport as __declspec(dllexport) only when a platform macro it recognises
// is set, and which macro that is changed with the SDK's age:
//
//   SDKs up to 2025-11-14:  #if defined(WIN32) || defined(WIN64)
//   SDKs after that:        #if defined(_WIN32)
//
// MSVC defines _WIN32 and _WIN64 itself but does NOT define WIN32 - that one comes from the build
// system. CMake happens to inject /DWIN32 for MSVC, so a CMake build against an older SDK usually
// gets away with it, but any other build (a raw MSBuild project, a different generator, someone
// clearing COMPILE_DEFINITIONS) silently produces a .ofx with an EMPTY export table: Resolve then
// loads the binary and reports that it contains no plugins, which is exactly the failure the
// reference guide in docs/reference/ describes. On Linux the same omission drops the symbols out
// of the dynamic table under -fvisibility=hidden.
//
// Both spellings below use only compiler-defined macros, so the entry points are exported whatever
// the SDK checkout says. Verified on this project by building against OFX 1.4 and current main and
// checking the export table of each result.
// ---------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#define AIBGR_OFX_ENTRY extern "C" __declspec(dllexport)
#else
#define AIBGR_OFX_ENTRY extern "C" __attribute__((visibility("default")))
#endif

namespace {

/// Called before anything else. ofxCore.h is explicit that a plugin MUST NOT call any OFX function
/// from here - it may only record the pointer. Suites are fetched later, on kOfxActionLoad.
void setHostFunc(OfxHost* hostStruct) {
    aibgr::ofx::HostBridge::instance().storeHost(hostStruct);
}

/// The action entry point. Signature is fixed by ofxCore.h:86: action, then handle, then args.
OfxStatus mainEntryFunc(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                        OfxPropertySetHandle outArgs) {
    return aibgr::ofx::pluginMain(action, handle, inArgs, outArgs);
}

/// One plugin per binary. Adding a second (for example a Matte-only variant) means extending
/// OfxGetNumberOfPlugins and this array together.
OfxPlugin gPlugin = {
    kOfxImageEffectPluginApi,
    kOfxImageEffectPluginApiVersion,
    AIBGR_PLUGIN_UNIQUE_ID,
    AIBGR_PLUGIN_VERSION_MAJOR,
    AIBGR_PLUGIN_VERSION_MINOR,
    setHostFunc,
    mainEntryFunc,
};

// The optional OfxSetHost export (ofxCore.h:661, added 2020) is deliberately NOT provided.
//
// A host that finds the symbol calls it first and treats a kOfxStatFailed return as "skip this
// plugin silently". The configuration that is known to load and render in Resolve Free 21.1 exports
// exactly the two functions below and carries the host pointer through OfxPlugin::setHost instead,
// so that is what we ship. If a future host ever needs the earlier hook, add it here and return
// kOfxStatOK unconditionally - never kOfxStatFailed.

} // namespace

AIBGR_OFX_ENTRY OfxPlugin* OfxGetPlugin(int nth) { return (nth == 0) ? &gPlugin : nullptr; }

AIBGR_OFX_ENTRY int OfxGetNumberOfPlugins(void) { return 1; }

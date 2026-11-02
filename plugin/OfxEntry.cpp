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

OfxPlugin* OfxGetPlugin(int nth) { return (nth == 0) ? &gPlugin : nullptr; }

int OfxGetNumberOfPlugins(void) { return 1; }

// SPDX-License-Identifier: MIT
#include "plugin/PluginPaths.h"

#include "aibgr/PluginIdentity.h"
#include "util/Log.h"
#include "util/Paths.h"

namespace aibgr {
namespace ofx {

std::string pluginDataDir() {
    const std::string base = programDataDir();
    if (base.empty()) {
        return {};
    }
    return joinPath(base, AIBGR_PLUGIN_NAME);
}

std::string modelsDir() {
    const std::string d = pluginDataDir();
    return d.empty() ? std::string() : joinPath(d, "models");
}

std::string defaultLogPath() {
    const std::string d = pluginDataDir();
    return d.empty() ? std::string() : joinPath(d, "log.txt");
}

bool ensurePluginDataDir() {
    const std::string d = pluginDataDir();
    if (d.empty()) {
        return false;
    }
    if (!ensureDirectory(d)) {
        AIBGR_LOGE("could not create plugin data directory '%s'", d.c_str());
        return false;
    }
    return true;
}

} // namespace ofx
} // namespace aibgr

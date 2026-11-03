// SPDX-License-Identifier: MIT
#include "plugin/PluginPaths.h"

#include "aibgr/PluginIdentity.h"
#include "util/Log.h"
#include "util/Paths.h"

#include <cstdio>

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

namespace {
bool canAppend(const std::string& path) {
    if (path.empty()) return false;
    FILE* file = nullptr;
#if defined(_WIN32)
    const std::wstring wide = widen(path);
    if (wide.empty() || _wfopen_s(&file, wide.c_str(), L"ab") != 0) return false;
#else
    file = std::fopen(path.c_str(), "ab");
#endif
    if (file == nullptr) return false;
    return std::fclose(file) == 0;
}
} // namespace

std::string selectWritableLogPath(const std::string& preferred, const std::string& fallback) {
    if (canAppend(preferred)) return preferred;
    if (canAppend(fallback)) return fallback;
    return {};
}

std::string preferredLogPath() {
    const std::string dir = moduleDir();
    return dir.empty() ? std::string() : joinPath(dir, "log.txt");
}

std::string configurePluginLog() {
    const std::string preferred = preferredLogPath();
    std::string selected = selectWritableLogPath(preferred, {});
    if (selected.empty() && ensurePluginDataDir()) {
        selected = selectWritableLogPath({}, joinPath(pluginDataDir(), "log.txt"));
    }
    if (selected.empty()) {
        const std::string cache = userCacheDir();
        const std::string userDir = cache.empty() ? std::string() : joinPath(cache, AIBGR_PLUGIN_NAME);
        if (!userDir.empty() && ensureDirectory(userDir)) {
            selected = selectWritableLogPath({}, joinPath(userDir, "log.txt"));
        }
    }
    Log::instance().setFilePath(selected);
    if (!selected.empty() && selected != preferred) {
        AIBGR_LOGI("plugin directory log unavailable; using fallback log: %s", selected.c_str());
    }
    return selected;
}

std::string defaultLogPath() { return Log::instance().filePath(); }

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

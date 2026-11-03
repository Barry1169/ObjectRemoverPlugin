// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace aibgr {
namespace ofx {

/// Writable per-machine data directory: %ProgramData%\AiBgRemover on Windows.
///
/// Kept outside the .ofx.bundle on purpose: Program Files needs administrator rights to write,
/// models are large and shared between hosts, and updating a model must not mean reinstalling the
/// plugin.
std::string pluginDataDir();

/// <pluginDataDir>/models - first place model files are looked for.
std::string modelsDir();

/// Prefer log.txt beside the .ofx; if unavailable use the writable data directory, then user cache.
/// Returns an empty string if no candidate can be opened. Never changes the directory permissions.
std::string selectWritableLogPath(const std::string& preferred, const std::string& fallback);
std::string preferredLogPath();
std::string configurePluginLog();

/// Actual selected path (empty if logging could not be initialized).
std::string defaultLogPath();

/// Creates pluginDataDir() if needed. Returns true if it is usable.
bool ensurePluginDataDir();

} // namespace ofx
} // namespace aibgr

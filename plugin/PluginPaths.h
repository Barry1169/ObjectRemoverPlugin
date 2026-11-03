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

/// <pluginDataDir>/log.txt - the action-trace log.
std::string defaultLogPath();

/// Creates pluginDataDir() if needed. Returns true if it is usable.
bool ensurePluginDataDir();

} // namespace ofx
} // namespace aibgr

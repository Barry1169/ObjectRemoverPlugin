// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

namespace aibgr {

/// All paths in this codebase are UTF-8 std::string. The conversion to/from the platform's
/// wide form happens only at the Win32 boundary, so a model or log path containing non-ASCII
/// characters (a Windows user profile named in Cyrillic, say) still works.

#if defined(_WIN32)
std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);
#endif

/// Directory that contains the running module (the .ofx). Uses GetModuleHandleExW with
/// FROM_ADDRESS on Windows and dladdr elsewhere, so no DllMain is required.
/// Returns an empty string if it cannot be determined.
std::string moduleDir();

/// Absolute path of the running module itself, or empty.
std::string modulePath();

std::string joinPath(std::string_view base, std::string_view leaf);

bool fileExists(std::string_view utf8Path);
bool directoryExists(std::string_view utf8Path);

/// Creates the directory and any missing parents. True if it exists afterwards.
bool ensureDirectory(std::string_view utf8Path);

/// %ProgramData% on Windows, /usr/local/share elsewhere. Empty if unavailable.
std::string programDataDir();

/// Per-user writable location: %LOCALAPPDATA% on Windows, $XDG_CACHE_HOME or ~/.cache elsewhere.
std::string userCacheDir();

/// Byte size of a file, or -1 on error.
long long fileSize(std::string_view utf8Path);

} // namespace aibgr

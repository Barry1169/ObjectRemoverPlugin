// SPDX-License-Identifier: MIT
#include "util/Paths.h"

#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <direct.h>
#include <io.h>
#else
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstdlib>
#include <vector>

namespace aibgr {

#if defined(_WIN32)

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                             static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                          needed);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                             static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                             nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                          needed, nullptr, nullptr);
    return out;
}

namespace {
bool selfModuleHandle(HMODULE* out) noexcept {
    // FROM_ADDRESS + UNCHANGED_REFCOUNT: no DllMain needed and no lifetime side effects.
    return ::GetModuleHandleExW(
               GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
               reinterpret_cast<LPCWSTR>(&selfModuleHandle), out) != 0;
}
} // namespace

std::string modulePath() {
    HMODULE mod = nullptr;
    if (!selfModuleHandle(&mod)) {
        return {};
    }
    std::vector<wchar_t> buf(MAX_PATH * 2);
    for (int attempt = 0; attempt < 4; ++attempt) {
        const DWORD n = ::GetModuleFileNameW(mod, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size()) {
            buf.resize(n);
            return narrow(std::wstring_view(buf.data(), n));
        }
        buf.resize(buf.size() * 2);
    }
    return {};
}

#else // POSIX

std::string modulePath() {
    Dl_info info{};
    if (::dladdr(reinterpret_cast<void*>(&modulePath), &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return std::string(info.dli_fname);
}

#endif

std::string moduleDir() {
    const std::string path = modulePath();
    if (path.empty()) {
        return {};
    }
    const std::size_t sep = path.find_last_of("/\\");
    if (sep == std::string::npos) {
        return {};
    }
    return path.substr(0, sep);
}

std::string joinPath(std::string_view base, std::string_view leaf) {
    if (base.empty()) {
        return std::string(leaf);
    }
    if (leaf.empty()) {
        return std::string(base);
    }
    const char last = base.back();
    const bool baseHasSep = (last == '/' || last == '\\');
    const char first = leaf.front();
    const bool leafHasSep = (first == '/' || first == '\\');
    std::string out(base);
    if (!baseHasSep && !leafHasSep) {
#if defined(_WIN32)
        out += '\\';
#else
        out += '/';
#endif
    } else if (baseHasSep && leafHasSep) {
        out.pop_back();
    }
    out += leaf;
    return out;
}

namespace {
bool statPath(std::string_view utf8Path, bool* isDir) noexcept {
#if defined(_WIN32)
    const std::wstring w = widen(utf8Path);
    if (w.empty()) {
        return false;
    }
    struct _stat64 st{};
    if (::_wstat64(w.c_str(), &st) != 0) {
        return false;
    }
    if (isDir != nullptr) {
        *isDir = (st.st_mode & _S_IFDIR) != 0;
    }
    return true;
#else
    std::string p(utf8Path);
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) {
        return false;
    }
    if (isDir != nullptr) {
        *isDir = S_ISDIR(st.st_mode);
    }
    return true;
#endif
}
} // namespace

bool fileExists(std::string_view utf8Path) {
    bool isDir = false;
    return statPath(utf8Path, &isDir) && !isDir;
}

bool directoryExists(std::string_view utf8Path) {
    bool isDir = false;
    return statPath(utf8Path, &isDir) && isDir;
}

long long fileSize(std::string_view utf8Path) {
#if defined(_WIN32)
    const std::wstring w = widen(utf8Path);
    if (w.empty()) {
        return -1;
    }
    struct _stat64 st {};
    if (::_wstat64(w.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long long>(st.st_size);
#else
    std::string p(utf8Path);
    struct stat st {};
    if (::stat(p.c_str(), &st) != 0) {
        return -1;
    }
    return static_cast<long long>(st.st_size);
#endif
}

namespace {
bool makeOneDirectory(std::string_view utf8Path) noexcept {
#if defined(_WIN32)
    const std::wstring w = widen(utf8Path);
    if (w.empty()) {
        return false;
    }
    if (::_wmkdir(w.c_str()) == 0) {
        return true;
    }
    return errno == EEXIST;
#else
    std::string p(utf8Path);
    if (::mkdir(p.c_str(), 0755) == 0) {
        return true;
    }
    return errno == EEXIST;
#endif
}
} // namespace

bool ensureDirectory(std::string_view utf8Path) {
    if (utf8Path.empty()) {
        return false;
    }
    if (directoryExists(utf8Path)) {
        return true;
    }
    // Walk the path creating each component. Handles both '/' and '\\' so a user-supplied
    // Windows path works even when built/tested on POSIX.
    std::string accumulated;
    accumulated.reserve(utf8Path.size());
    for (std::size_t i = 0; i < utf8Path.size(); ++i) {
        const char c = utf8Path[i];
        if ((c == '/' || c == '\\') && !accumulated.empty()) {
            // Keep a drive-relative root like "C:" intact before appending the separator.
            if (!makeOneDirectory(accumulated)) {
                return false;
            }
        }
        accumulated += c;
    }
    if (!accumulated.empty() && !makeOneDirectory(accumulated)) {
        return false;
    }
    return directoryExists(utf8Path);
}

std::string programDataDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (::GetEnvironmentVariableW(L"ProgramData", buf, MAX_PATH) > 0) {
        return narrow(buf);
    }
    return std::string("C:\\ProgramData");
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::string(xdg);
    }
    return std::string("/usr/local/share");
#endif
}

std::string userCacheDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH) > 0) {
        return narrow(buf);
    }
    if (::GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH) > 0) {
        return joinPath(narrow(buf), "AppData\\Local");
    }
    return {};
#else
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::string(xdg);
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        const struct passwd* pw = ::getpwuid(::getuid());
        home = (pw != nullptr) ? pw->pw_dir : nullptr;
    }
    if (home == nullptr || *home == '\0') {
        return {};
    }
    return joinPath(home, ".cache");
#endif
}

} // namespace aibgr

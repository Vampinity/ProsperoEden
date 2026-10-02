// SPDX-License-Identifier: GPL-3.0-or-later
// Where ProsperoEden keeps things. With filesystem access (elevation/elevation.hpp, requested
// first thing in main) the app uses real console paths:
//   app folder     the install location, normally /data/homebrew/PPSA99008 (a game tile's
//                  own title ID instead: game_tile.h)
//   data           /data/prosperoeden: config/ (prosperoeden.json), logs/, covers/, user/
// Without it (no elfldr, or the request failed) the sandbox paths stay: /app0 and /download0.
#pragma once
#include <cstdio>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace Eden {
inline constexpr const char* kDataDir = "/data/prosperoeden";
inline constexpr const char* kDefaultAssetsDir = "/data/prosperoeden";
inline constexpr const char* kTitleId = "PPSA99008";

// The title this copy of the app runs as: PPSA99008, or a game tile's own ID (game_tile.h).
// Read from /app0 before filesystem access is requested, which takes /app0 away.
inline std::string& RunningTitleId() {
    static std::string title = kTitleId;
    return title;
}
inline bool ValidTileTitleId(std::string_view id) {
    if (id.size() != 9 || id.substr(0, 6) != "PPSA98") return false;
    for (char c : id.substr(6))
        if (c < '0' || c > '9') return false;
    return true;
}
inline void ReadRunningTitleId() {
    FILE* file = std::fopen("/app0/sce_sys/param.json", "rb");
    if (!file) return;
    char text[8192]{};
    const std::size_t size = std::fread(text, 1, sizeof(text) - 1, file);
    std::fclose(file);
    const std::string_view json{text, size};
    const auto key = json.find("\"titleId\"");
    if (key == json.npos) return;
    const auto open = json.find('"', json.find(':', key));
    if (open == json.npos || open + 10 > json.size() || json[open + 10] != '"') return;
    const std::string_view id = json.substr(open + 1, 9);
    if (ValidTileTitleId(id)) RunningTitleId() = std::string{id};
}
inline std::string InstallDir() { return "/data/homebrew/" + RunningTitleId(); }

// Filesystem access requested at startup: -1 not requested, 0 granted, otherwise the
// elevation::Status that refused it.
inline int& FilesystemAccessStatus() {
    static int status = -1;
    return status;
}
inline bool FilesystemAccess() { return FilesystemAccessStatus() == 0; }

inline bool FileExists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}
inline bool DirectoryExists(const std::string& path) {
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// The app's own files (eboot, ui/, development markers).
inline const std::string& AppDir() {
    static const std::string directory = [] {
        if (!FilesystemAccess()) return std::string{"/app0"};
        // A console root has no /app0: the sandbox mounts it from the install folder.
        for (const std::string& candidate : {InstallDir(), "/mnt/sandbox/" + RunningTitleId() + "_000/app0"})
            if (FileExists(candidate + "/eboot.bin")) return candidate;
        return InstallDir();
    }();
    return directory;
}
inline std::string AppFile(std::string_view name) { return AppDir() + "/" + std::string(name); }

// Settings, logs, covers and Eden's user folder.
inline std::string ConfigDir() { return FilesystemAccess() ? std::string{kDataDir} + "/config" : "/download0/prosperoeden"; }
inline std::string LogsDir() { return FilesystemAccess() ? std::string{kDataDir} + "/logs" : "/download0/eden-headless-g7"; }
inline std::string CoversDir() { return FilesystemAccess() ? std::string{kDataDir} + "/covers" : "/download0/prosperoeden/covers"; }
inline std::string UserDir() { return FilesystemAccess() ? std::string{kDataDir} + "/user" : "/download0/eden-headless-g7/user"; }
inline std::string ConfigFile(std::string_view name) { return ConfigDir() + "/" + std::string(name); }
inline std::string LogFile(std::string_view name) { return LogsDir() + "/" + std::string(name); }

inline bool ValidAssetsDir(std::string_view path) {
    if (path.empty() || path.size() > 240 || path.front() != '/') return false;
    if (path.size() > 1 && path.back() == '/') return false;
    for (unsigned char c : path)
        if (c < 32 || c == 127 || c == '\\') return false;
    for (std::size_t start = 1; start <= path.size();) {
        const std::size_t end = path.find('/', start);
        const std::string_view part = path.substr(start, end == std::string_view::npos ? path.npos : end - start);
        if (part.empty() || part == "." || part == "..") return path == "/";
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

} // namespace Eden

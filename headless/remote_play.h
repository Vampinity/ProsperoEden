// SPDX-License-Identifier: GPL-3.0-or-later
// Portal mode: whether a Remote Play client (a PlayStation Portal, the PS Remote Play app) is
// streaming the console when a game starts, and the video settings worth lowering if so.
//
// Remote Play sends at most 1920x1080 at 60 frames per second, so while it runs a game gains
// nothing from drawing more: an output above 1080p, a 120 Hz output or a resolution above 1x
// only cost GPU time the stream throws away. Portal mode caps those three for that session and
// changes nothing else (renderer, filter, Handheld / Docked, mods). Settings that are already
// within the caps are left alone, so on such a setup Portal mode does nothing at all.
// The saved settings are not touched; the next game started without Remote Play uses them as is.
// It is checked once, as a game starts. "video": { "portal_mode": false } switches it off.
//
// The console's libSceRemoteplay is loaded when needed rather than linked, so a firmware without
// it (or a sandbox that refuses it) only means Remote Play reads as off.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#if defined(__PROSPERO__)
#include <unistd.h>
#endif

#if defined(__PROSPERO__)
extern "C" int sceKernelLoadStartModule(const char* path, std::size_t argc, const void* argv, std::uint32_t flags,
                                        void* option, int* result);
extern "C" int sceKernelDlsym(int handle, const char* symbol, void** address);
extern "C" int sceUserServiceInitialize(void* parameters);
#endif

namespace Eden::RemotePlay {
// What a check found, for the session log.
struct Status {
    bool connected = false;
    int module = 0;    // the module handle, or the loader's error
    int result = 0;    // the last sceRemoteplay call's return code
    int user = -1;     // the user the stream belongs to
};

#if defined(__PROSPERO__)
namespace detail {
struct LoginUserIdList {
    std::int32_t user_id[4];
};
}
extern "C" int sceUserServiceGetLoginUserIdList(detail::LoginUserIdList* list);
// The kernel's randomized_path call: the name the sandbox gives the system folder.
inline constexpr int kRandomizedPath = 602;

inline Status Check() {
    Status status;
    // An app's sandbox shows the system folder under a random name ("/<word>/common/lib"), not
    // as /system; the SDK's loader looks there the same way (crt/rtld.c, __rtld_find_file).
    char word[0x100] = {};
    unsigned long word_bytes = sizeof(word) - 1;
    std::string paths[4];
    int count = 0;
    if (syscall(kRandomizedPath, 0, word, &word_bytes) == 0 && word[0]) {
        paths[count++] = std::string("/") + word + "/common/lib/libSceRemoteplay.sprx";
        paths[count++] = std::string("/") + word + "/priv/lib/libSceRemoteplay.sprx";
    }
    paths[count++] = "/system/common/lib/libSceRemoteplay.sprx";
    paths[count++] = "/system/priv/lib/libSceRemoteplay.sprx";
    int handle = -1;
    for (int i = 0; i < count && handle < 0; ++i)
        handle = sceKernelLoadStartModule(paths[i].c_str(), 0, nullptr, 0, nullptr, nullptr);
    status.module = handle;
    if (handle < 0) return status;
    using GetConnectionStatus = int (*)(int user, int* connected);
    using Initialize = int (*)(void* heap, std::size_t bytes);
    void* get = nullptr;
    void* initialize = nullptr;
    if (sceKernelDlsym(handle, "sceRemoteplayGetConnectionStatus", &get) < 0 || !get) {
        status.result = -1;
        return status;
    }
    (void)sceKernelDlsym(handle, "sceRemoteplayInitialize", &initialize);
    (void)sceUserServiceInitialize(nullptr);
    detail::LoginUserIdList users;
    for (auto& user : users.user_id) user = -1;
    if ((status.result = sceUserServiceGetLoginUserIdList(&users)) < 0) return status;
    // The library answers only once it is initialized; a title that never initialized it gets an
    // error back instead, so initialize it then and ask again. Its heap stays with the module.
    bool initialized = false;
    for (int attempt = 0; attempt < 2 && !status.connected; ++attempt) {
        for (const int user : users.user_id) {
            if (user < 0) continue;
            int connected = 0;
            status.result = reinterpret_cast<GetConnectionStatus>(get)(user, &connected);
            if (status.result == 0 && connected == 1) {
                status.connected = true;
                status.user = user;
                break;
            }
        }
        if (status.result >= 0 || !initialize || initialized) break;
        alignas(16) static unsigned char heap[64 * 1024];
        initialized = true;
        status.result = reinterpret_cast<Initialize>(initialize)(heap, sizeof(heap));
        if (status.result < 0) break;
    }
    return status;
}
#else
inline Status Check() { return {}; }
#endif

inline std::string Describe(const Status& status) {
    char text[160];
    if (status.connected)
        std::snprintf(text, sizeof(text), "Remote Play is streaming (user %d)", status.user);
    else if (status.module < 0)
        std::snprintf(text, sizeof(text), "Remote Play not checked (module 0x%08x)", unsigned(status.module));
    else
        std::snprintf(text, sizeof(text), "Remote Play not streaming (0x%08x)", unsigned(status.result));
    return text;
}

// The caps, as indices into settings_store.h's key tables: 1x, 60 Hz, 1080p.
struct Video {
    int resolution;
    int refresh;
    int output;
};
// What the session uses with Portal mode on, and a summary of what that changed (empty when the
// settings were within the caps already).
inline Video Cap(Video video, std::string& changes, int native_resolution) {
    const auto note = [&](const char* text) { changes += changes.empty() ? text : std::string(", ") + text; };
    if (video.resolution > native_resolution) { video.resolution = native_resolution; note("resolution 1x"); }
    if (video.refresh > 0) { video.refresh = 0; note("60 Hz"); }
    if (video.output > 0) { video.output = 0; note("output 1080p"); }
    return video;
}
} // namespace Eden::RemotePlay

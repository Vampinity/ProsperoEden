// SPDX-License-Identifier: GPL-3.0-or-later
// What is playing, for tools outside the console (a home dashboard reads it over the PS5's FTP
// server): config/now-playing.json in the data folder, /data/prosperoeden/config/now-playing.json.
//   playing     true from the moment a game has loaded until its session ends
//   system      "Nintendo Switch"
//   title       the game's name as the Library shows it (library.json), else its file name
//   title_id    the game's 16 hex digit title ID; empty when unknown
//   file        its file name in roms/
//   cover       the cached cover image on the console (covers/), empty without one
//   ps5_title   the title the app runs as: PPSA99008, or a game tile's own ID (game_tile.h)
//   started_at  Unix seconds when the game loaded
//   updated_at  Unix seconds of this write
// When the game ends the file keeps the last game with "playing": false. The app clears a
// stale "playing" when it starts, so after a crash or a forced close it is only right again once
// the app runs; a reader should also check that the PS5 is running ProsperoEden or a tile.
#pragma once
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <string>

#include "settings_store.h"

namespace Eden::NowPlaying {

inline std::string File() { return ConfigFile("now-playing.json"); }

// A JSON file as an object; empty when it is missing or not one. (Settings::Load would build the
// settings document from the earlier text files instead.)
inline Settings::Json Read(const std::string& path) {
    std::string text;
    if (!Settings::ReadFile(path, text)) return Settings::Json::object();
    Settings::Json document = Settings::Json::parse(text, nullptr, false);
    return document.is_object() ? document : Settings::Json::object();
}

inline std::string Text(const Settings::Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

inline bool Start(const std::string& rom_path, std::uint64_t title_id) {
    const std::string file = std::filesystem::path(rom_path).filename().string();
    std::string title = std::filesystem::path(rom_path).stem().string();
    std::string cover;
    const Settings::Json library = Read(ConfigFile("library.json"));
    if (const auto games = library.find("games"); games != library.end() && games->is_array())
        for (const auto& game : *games)
            if (game.is_object() && Text(game, "file") == file) {
                if (const std::string name = Text(game, "name"); !name.empty()) title = name;
                cover = Text(game, "cover");
                break;
            }
    const auto now = static_cast<long long>(std::time(nullptr));
    return Settings::Write({{"playing", true},
                            {"system", "Nintendo Switch"},
                            {"title", title},
                            {"title_id", title_id ? Settings::TitleKey(title_id) : std::string{}},
                            {"file", file},
                            {"cover", cover},
                            {"ps5_title", RunningTitleId()},
                            {"started_at", now},
                            {"updated_at", now}},
                           File());
}

// The last game stays in the file; only "playing" and "updated_at" change.
inline bool Stop() {
    Settings::Json document = Read(File());
    if (const auto playing = document.find("playing"); playing != document.end() && *playing == false)
        return true;
    document["playing"] = false;
    document["updated_at"] = static_cast<long long>(std::time(nullptr));
    return Settings::Write(document, File());
}

} // namespace Eden::NowPlaying

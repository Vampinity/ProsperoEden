// SPDX-License-Identifier: GPL-3.0-or-later
// Game tiles: a copy of the app installed under its own title ID (PPSA98000-PPSA98999) with one
// game's cover, background and name in its sce_sys, so the PS5's home screen and control center
// show that game while it runs, as they do for a PS5 game. tools/game-tiles makes them.
// The tile's tile.txt names its game: "rom=<file name in roms/>" and "title=<16 hex digits>"
// (either is enough; the file name is tried first). A tile boots its game at once and closes
// with it; Select + L1 goes back to the PS5's home screen.
// ProsperoEden itself boots a game the same way when it is started with one in its arguments,
// for example by a launcher on the network (websrv's /launch?titleId=PPSA99008&args=title=<ID>):
// "title=<16 hex digits>" or a bare title ID, or "rom=<file name>" (a name with spaces may come
// split into several arguments; they are joined again).
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "assets_dir.h"
#include "metadata_bridge.h"
#include "native_directory.h"

namespace Eden::Tile {

struct Game {
    std::string rom;
    std::uint64_t title_id = 0;
    bool Set() const { return title_id != 0 || !rom.empty(); }
    std::string Name() const {
        if (!rom.empty()) return rom;
        return Settings::TitleKey(title_id);
    }
};

inline bool TitleText(std::string_view text) {
    return text.size() == 16 && text.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos;
}

// One "rom=" or "title=" line of tile.txt, or one launch argument.
inline void ReadSetting(Game& game, const std::string& line) {
    if (line.starts_with("rom=")) {
        if (ValidRomFilename(line.substr(4))) game.rom = line.substr(4);
    } else if (line.starts_with("title=") && TitleText(std::string_view{line}.substr(6))) {
        game.title_id = std::strtoull(line.c_str() + 6, nullptr, 16);
    }
}

// The game this copy of the app boots; none when it runs as ProsperoEden itself.
inline Game Read() {
    Game game;
    if (RunningTitleId() == kTitleId) return game;
    std::ifstream file(AppFile("tile.txt"));
    for (std::string line; std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        ReadSetting(game, line);
    }
    return game;
}

// The game named in the app's launch arguments; none without one.
inline Game FromArguments(int argc, char** argv) {
    Game game;
    for (int i = 0; i < argc && argv[i]; ++i) {
        std::string argument = argv[i];
        if (TitleText(argument)) argument = "title=" + argument;
        if (argument.starts_with("rom="))
            while (i + 1 < argc && argv[i + 1] && !std::string_view{argv[i + 1]}.starts_with("title=") &&
                   !ValidRomFilename(argument.substr(4)))
                argument += std::string{" "} + argv[++i];
        ReadSetting(game, argument);
    }
    return game;
}

// The game's ROM in the game files folder, or empty when it is not there.
inline std::string FindRom(const Game& game) {
    if (!game.rom.empty()) {
        const std::string path = AssetsPath("roms/" + game.rom);
        if (FileExists(path)) return path;
    }
    if (game.title_id == 0) return {};
    // Renamed or moved: the title ID in the file name first, then the ROM's own metadata.
    const std::string key = Settings::TitleKey(game.title_id);
    std::error_code error;
    const auto entries = ReadNativeDirectory(AssetsPath("roms"), error);
    for (const auto& entry : entries) {
        const std::string filename = entry.path().filename().string();
        if (!ValidRomFilename(filename)) continue;
        const std::string path = AssetsPath("roms/" + filename);
        if (!FileExists(path)) continue;
        std::string upper = filename;
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (upper.find(key) != std::string::npos) return path;
    }
    for (const auto& entry : entries) {
        const std::string filename = entry.path().filename().string();
        if (!ValidRomFilename(filename)) continue;
        const std::string path = AssetsPath("roms/" + filename);
        if (FileExists(path) && eden_game_title_id(path.c_str()) == game.title_id) return path;
    }
    return {};
}

} // namespace Eden::Tile

/*
 * ProsperoEden game tile starter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A game tile is this small app with one game's name and icon. It asks websrv on the console
 * (http://127.0.0.1:8080/hbldr) to run /data/prosperoeden/tile-launch.elf with the game named in
 * /app0/tile.txt ("title=<16 hex digits>" or "rom=<file name>"), then closes itself. The launcher
 * waits until this app is gone and starts ProsperoEden (PPSA99008), which boots the game.
 * websrv's /launch is not used: it closes this app and starts the next at once, which fails while
 * this app is still closing (PS5 error CE-105773-3). Anything that goes wrong is shown on screen.
 */

#include "demo_renderer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>

extern "C" int sceSystemServiceLoadExec(const char *path, const char **arguments);

namespace
{
const char launcher[] = "/data/prosperoeden/tile-launch.elf";
const char *status = "STARTING THE GAME";
const char *detail = "";

// The game named in tile.txt as one launch argument: its "title=" line, else its "rom=" line.
bool read_setting(char *argument, std::size_t size)
{
    std::FILE *file = std::fopen("/app0/tile.txt", "rb");
    if (file == nullptr)
        return false;
    char line[512];
    bool title = false;
    argument[0] = 0;
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        line[std::strcspn(line, "\r\n")] = 0;
        if (std::strncmp(line, "title=", 6) == 0) {
            std::snprintf(argument, size, "%s", line);
            title = true;
        } else if (std::strncmp(line, "rom=", 4) == 0 && !title) {
            std::snprintf(argument, size, "%s", line);
        }
    }
    std::fclose(file);
    return argument[0] != 0;
}

// websrv splits its args at spaces unless a backslash escapes them; the result is URL-encoded.
void query_value(const char *argument, char *out, std::size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    std::size_t length = 0;
    auto put = [&](unsigned char c) {
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                           c == '-' || c == '_' || c == '.' || c == '~';
        if (length + 4 >= size)
            return;
        if (plain) {
            out[length++] = static_cast<char>(c);
        } else {
            out[length++] = '%';
            out[length++] = hex[c >> 4];
            out[length++] = hex[c & 15];
        }
    };
    for (const char *c = argument; *c; ++c) {
        if (*c == ' ' || *c == '\\')
            put('\\');
        put(static_cast<unsigned char>(*c));
    }
    out[length] = 0;
}

// websrv's HTTP status for running the launcher: 200 when it runs, 0 when websrv did not answer.
int ask_websrv(const char *argument)
{
    const int connection = socket(AF_INET, SOCK_STREAM, 0);
    if (connection < 0)
        return 0;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int status = 0;
    if (connect(connection, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
        static char value[2048];
        static char request[2400];
        query_value(argument, value, sizeof(value));
        const int length = std::snprintf(request, sizeof(request),
                                         "GET /hbldr?daemon=1&path=%s&args=%s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n",
                                         launcher, value);
        if (length > 0 && send(connection, request, static_cast<std::size_t>(length), 0) == length) {
            char reply[64] = {};
            std::size_t got = 0;
            while (got + 1 < sizeof(reply)) {
                const auto n = recv(connection, reply + got, sizeof(reply) - 1 - got, 0);
                if (n <= 0)
                    break;
                got += static_cast<std::size_t>(n);
                if (std::strchr(reply, '\n') != nullptr)
                    break;
            }
            if (std::strncmp(reply, "HTTP/1.", 7) == 0 && got > 12)
                status = std::atoi(reply + 9);
        }
    }
    close(connection);
    return status;
}

void draw_scene(ps5::demo::Canvas &canvas) noexcept
{
    using ps5::demo::Color;
    canvas.clear(Color::background);
    canvas.text(120, 420, status, 8, Color::white);
    canvas.text(120, 560, detail, 4, Color::cyan);
}
} // namespace

int main()
{
    static char argument[600];
    if (!read_setting(argument, sizeof(argument))) {
        status = "THIS TILE NAMES NO GAME";
        detail = "TILE.TXT IS MISSING. MAKE THE TILE AGAIN.";
    } else {
        const int answer = ask_websrv(argument);
        if (answer == 200) {
            // The launcher starts ProsperoEden once this app has closed.
            sceSystemServiceLoadExec("exit", nullptr);
            status = "CLOSING";
        } else if (answer == 0) {
            status = "WEBSRV IS NOT RUNNING";
            detail = "LOAD WEBSRV ON PORT 8080, THEN OPEN THIS TILE AGAIN.";
        } else {
            status = "THE TILE LAUNCHER IS MISSING";
            detail = "RUN MAKE-TILES.PY AGAIN, THEN OPEN THIS TILE AGAIN.";
        }
    }
    ps5::demo::run(draw_scene, status);
}

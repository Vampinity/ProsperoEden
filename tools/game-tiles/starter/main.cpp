/*
 * ProsperoEden game tile starter.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A game tile is this small app with one game's name and icon. It asks websrv on the console
 * (http://127.0.0.1:8080/launch) to start ProsperoEden (PPSA99008) with the game named in
 * /app0/tile.txt ("title=<16 hex digits>" or "rom=<file name>"). websrv closes this app and
 * starts ProsperoEden, which boots the game. If websrv is not running, it says so on screen.
 */

#include "demo_renderer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <cstddef>

namespace
{
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

bool ask_websrv(const char *argument)
{
    const int connection = socket(AF_INET, SOCK_STREAM, 0);
    if (connection < 0)
        return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bool sent = false;
    if (connect(connection, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0) {
        static char value[2048];
        static char request[2304];
        query_value(argument, value, sizeof(value));
        const int length = std::snprintf(request, sizeof(request),
                                         "GET /launch?titleId=PPSA99008&args=%s HTTP/1.0\r\nHost: 127.0.0.1\r\n\r\n",
                                         value);
        sent = length > 0 && send(connection, request, static_cast<std::size_t>(length), 0) == length;
        // websrv closes this app as it starts ProsperoEden; the reply rarely arrives.
        char reply[64];
        (void)recv(connection, reply, sizeof(reply), 0);
    }
    close(connection);
    return sent;
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
    } else if (!ask_websrv(argument)) {
        status = "WEBSRV IS NOT RUNNING";
        detail = "LOAD WEBSRV ON PORT 8080, THEN OPEN THIS TILE AGAIN.";
    }
    ps5::demo::run(draw_scene, status);
}

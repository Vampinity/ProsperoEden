#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a PS5 home screen tile for each game of the console's emulators, over FTP.

  tools/game-tiles/make-tiles.py HOST STARTER_DIR [--port 2121] [--only NAME] [--dry-run]
                                 [--label badge|text|none] [--console NAME] [--force]
                                 [--art DIR] [--prune] [--remove NAME]

STARTER_DIR is the tile starter folder (tools/game-tiles/starter, about 1.7 MB): each tile is a
small app, installed as /data/homebrew/PPSA98001-PPSA98999 with the game's name and cover in its
sce_sys and a tile.txt naming the emulator and its launch arguments, that hands them to the tile
launcher (/data/prosperoeden/tile-launch.elf, installed from STARTER_DIR) through websrv and
closes; the launcher starts the emulator with that game once the tile is gone. The 4K backgrounds
and sound are never copied.

The games are the ROMs of each installed emulator in EMULATORS: ProsperoEden (PPSA99008, the
.nsp/.xci files in the roms folder of its Settings > Game files folder, started with
--rom <file> --exit-after-game) and Mupen64Plus (PPSA99064, the files in /data/mupen64plus/roms,
started with the ROM's path). The game's name and cover are the ones the emulator keeps;
a PNG in the --art folder named like the game ("Super Mario 64.png") is used as its picture
instead. Which tile belongs to which game is kept in /data/prosperoeden/config/tiles.json, so
running this again updates the same tiles and only adds new ones; a tile that is already up to
date is skipped, so it can run on a schedule. --prune removes the tiles of games that are no
longer there, --remove the tiles whose name contains a text; then delete the tile on the home
screen too (Options > Delete). Needs a running FTP server on the console (the Payload SDK's
ftpsrv, port 2121) and Python 3 only.

Each tile has the game's own name and cover, and the home screen shows its usual PS5 label. With
--label badge it also gets a console badge in the top-left corner of its picture
(badges/<console>.png, next to this script), or with --label text the console in front of its name
("[Switch] ..."); tiles.json names each game's console either way. Its name and picture are also put where the home screen keeps its own copies
(/user/app and /user/appmeta). Restart the PS5 to see changed names and icons.
"""
import argparse
import ftplib
import io
import json
import pathlib
import re
import struct
import sys
import zlib

CONFIG = '/data/prosperoeden/config'
HOMEBREW = '/data/homebrew'
FIRST_TILE, LAST_TILE = 98001, 98999
ICON_SIZE = 512
SKIPPED = {'sce_sys/pic0.dds', 'sce_sys/pic1.dds', 'sce_sys/snd0.at9'}
APPS, APPMETA = '/user/app', '/user/appmeta'
LAUNCHER = 'tile-launch.elf'  # goes to CONFIG/.., not into the tiles
BADGES = pathlib.Path(__file__).resolve().parent / 'badges'
BADGE_MARGIN = 16
# The console a tile's game is for is shown on its tile, the way the home screen marks PS4 games
# (see --label). It comes from the game's own "console", "platform" or
# "system" field in the library, else from its file's extension, else from the emulator that runs
# it. Short labels, so the name still fits under the tile.
CONSOLES = {
    'switch': 'Switch', '3ds': '3DS', 'ds': 'DS', 'gba': 'GBA', 'gbc': 'Game Boy Color',
    'gb': 'Game Boy', 'gamecube': 'GameCube', 'wii': 'Wii', 'wiiu': 'Wii U', 'n64': 'N64',
    'snes': 'SNES', 'nes': 'NES', 'ps1': 'PS1', 'ps2': 'PS2', 'ps3': 'PS3', 'psp': 'PSP',
    'vita': 'PS Vita', 'dreamcast': 'Dreamcast', 'saturn': 'Saturn', 'megadrive': 'Mega Drive',
    'mastersystem': 'Master System', 'xbox': 'Xbox', 'xbox360': 'Xbox 360', 'arcade': 'Arcade',
    'pc': 'PC',
}
ALIASES = {
    'nintendo switch': 'switch', 'nx': 'switch', 'nintendo 3ds': '3ds', 'nintendo ds': 'ds',
    'game boy advance': 'gba', 'gameboy advance': 'gba', 'game boy color': 'gbc',
    'gameboy color': 'gbc', 'game boy': 'gb', 'gameboy': 'gb', 'nintendo gamecube': 'gamecube',
    'gc': 'gamecube', 'nintendo wii': 'wii', 'nintendo wii u': 'wiiu', 'wii u': 'wiiu',
    'nintendo 64': 'n64', 'super nintendo': 'snes', 'super nes': 'snes', 'super famicom': 'snes',
    'famicom': 'nes', 'nintendo entertainment system': 'nes', 'playstation': 'ps1', 'psx': 'ps1',
    'ps': 'ps1', 'playstation 2': 'ps2', 'playstation 3': 'ps3', 'playstation portable': 'psp',
    'playstation vita': 'vita', 'ps vita': 'vita', 'psvita': 'vita', 'sega dreamcast': 'dreamcast',
    'sega saturn': 'saturn', 'genesis': 'megadrive', 'sega genesis': 'megadrive',
    'mega drive': 'megadrive', 'sega mega drive': 'megadrive', 'master system': 'mastersystem',
    'sega master system': 'mastersystem', 'xbox 360': 'xbox360', 'mame': 'arcade',
    'fbneo': 'arcade', 'windows': 'pc', 'steam': 'pc', 'linux': 'pc', 'dos': 'pc',
}
EXTENSIONS = {
    'switch': '.nsp .nsz .xci .xcz .nro .nca', '3ds': '.3ds .cia .cci .cxi', 'ds': '.nds',
    'gba': '.gba', 'gbc': '.gbc', 'gb': '.gb', 'gamecube': '.gcm .gcz .rvz', 'wii': '.wbfs .wad',
    'wiiu': '.wua .wux .rpx', 'n64': '.n64 .z64 .v64', 'snes': '.sfc .smc', 'nes': '.nes',
    'psp': '.cso', 'vita': '.vpk', 'dreamcast': '.gdi .cdi', 'megadrive': '.md .gen .smd',
    'mastersystem': '.sms', 'xbox360': '.xex', 'pc': '.exe .lnk .url',
}
EXTENSION_CONSOLE = {ext: key for key, exts in EXTENSIONS.items() for ext in exts.split()}
# The emulators whose games get tiles: where their ROMs and covers are, and the console their
# games are for when a game's file does not say (.iso, .chd and .bin are used by many consoles).
PROSPEROEDEN, MUPEN64PLUS = 'PPSA99008', 'PPSA99064'
EMULATORS = {
    PROSPEROEDEN: {'name': 'ProsperoEden', 'console': 'switch', 'extensions': ('.nsp', '.xci')},
    MUPEN64PLUS: {'name': 'Mupen64Plus', 'console': 'n64', 'roms': '/data/mupen64plus/roms',
                  'covers': '/data/mupen64plus/covers', 'extensions': ('.z64', '.n64', '.v64', '.rom', '.zip')},
}
EMULATOR_CONSOLE = {app_id: emulator['console'] for app_id, emulator in EMULATORS.items()}
PROSPEROEDEN_DATA = '/data/prosperoeden'


def connect(host, port):
    client = ftplib.FTP()
    client.connect(host, port, timeout=60)
    client.login()
    client.voidcmd('TYPE I')  # binary, which SIZE needs
    return client


def delete(client, path):
    """Deletes a file; ftpsrv answers DELE with 226 instead of 250, which ftplib rejects."""
    try:
        client.delete(path)
    except ftplib.error_reply as reply:
        if not str(reply).startswith('2'):
            raise
    except ftplib.error_perm:
        pass  # not there


def rename(client, source, target):
    """Renames a file, accepting any 2xx/3xx reply the server gives."""
    try:
        client.rename(source, target)
    except ftplib.error_reply as reply:
        if not str(reply)[:1] in '23':
            raise


def remote_files(client, path, prefix='', depth=0):
    """Files under a console folder, relative to it; empty when it does not exist.

    ftpsrv ignores MLSD's path argument and always lists the current folder, so this changes into
    each folder and lists it without one.
    """
    found = []
    if depth > 8:
        return found
    try:
        client.cwd(path)
        entries = list(client.mlsd())
    except ftplib.error_perm:
        return found
    finally:
        client.cwd('/')
    for name, facts in entries:
        if name in ('.', '..'):
            continue
        if facts.get('type') == 'dir':
            found += remote_files(client, f'{path}/{name}', f'{prefix}{name}/', depth + 1)
        elif facts.get('type') == 'file':
            found.append(f'{prefix}{name}')
    return found


def exists(client, path):
    try:
        client.size(path)
        return True
    except ftplib.error_perm:
        return False


def set_icon(client, app_id, icon):
    """Puts icon on an installed app: its own copy, the installed copy and the home screen's.

    The home screen draws the copy in /user/appmeta, made when the app was first registered, so
    changing only the app's own icon0.png does not show.
    """
    changed = False
    for path in (f'{HOMEBREW}/{app_id}/sce_sys/icon0.png', f'{APPS}/{app_id}/sce_sys/icon0.png',
                 f'{APPMETA}/{app_id}/icon0.png'):
        if exists(client, path) and read_remote(client, path) != icon:
            write_remote(client, path, icon)
            changed = True
    return changed


def console_key(value):
    """The CONSOLES key for a console's name ("Nintendo Switch", "switch", "Switch"), or None."""
    value = str(value or '').strip().lower()
    if value in CONSOLES:
        return value
    return ALIASES.get(value) or next((k for k, name in CONSOLES.items() if name.lower() == value), None)


def game_console(game, emulator='PPSA99008'):
    """The CONSOLES key for a Library game, or None when it is not known."""
    for field in ('console', 'platform', 'system'):
        key = console_key(game.get(field))
        if key:
            return key
    extension = pathlib.PurePosixPath(game.get('file', '')).suffix.lower()
    return EXTENSION_CONSOLE.get(extension) or EMULATOR_CONSOLE.get(emulator)


def console_label(game, emulator='PPSA99008'):
    """The short console name for a Library game (see CONSOLES), or '' when it is not known."""
    return CONSOLES.get(game_console(game, emulator), '')


def restore_icon(client, app_id):
    """Shows an app's own icon0.png on the home screen again (see set_icon)."""
    own = read_remote(client, f'{HOMEBREW}/{app_id}/sce_sys/icon0.png')
    if own and set_icon(client, app_id, own):
        print(f'  {app_id} shows its own icon again')


def sync_copies(client, app_id, param_data, icon):
    """Puts a tile's param.json and icon0.png in the home screen's copies (/user/app, /user/appmeta).

    The home screen keeps the name and picture from when the tile was first registered there.
    """
    for path in (f'{APPS}/{app_id}/sce_sys/param.json', f'{APPMETA}/{app_id}/param.json'):
        if exists(client, path) and read_remote(client, path) != param_data:
            write_remote(client, path, param_data)
            print(f'  updated {path}')
    if icon and set_icon(client, app_id, icon):
        print(f'  updated the home screen picture of {app_id}')


def read_remote(client, path):
    buffer = io.BytesIO()
    try:
        client.retrbinary(f'RETR {path}', buffer.write)
    except ftplib.error_perm:
        return None
    return buffer.getvalue()


def write_remote(client, path, data):
    client.storbinary(f'STOR {path}.partial', io.BytesIO(data))
    delete(client, path)
    rename(client, f'{path}.partial', path)


def ensure_directory(client, path):
    current = ''
    for part in pathlib.PurePosixPath(path).parts[1:]:
        current += '/' + part
        try:
            client.mkd(current)
        except ftplib.error_perm:
            pass  # already there


# Covers are TGA files (truecolor, plain or RLE); the tile's icon0.png must be a 512x512 PNG.
def read_tga(data):
    id_length, colormap_type, image_type = data[0], data[1], data[2]
    width, height, depth, descriptor = struct.unpack_from('<HHBB', data, 12)
    if colormap_type != 0 or image_type not in (2, 10) or depth not in (24, 32):
        raise ValueError(f'unsupported TGA (type {image_type}, {depth} bits)')
    size = depth // 8
    offset = 18 + id_length
    count = width * height
    pixels = bytearray()
    if image_type == 2:
        pixels = data[offset:offset + count * size]
    else:
        while len(pixels) < count * size:
            header = data[offset]
            offset += 1
            run = (header & 0x7F) + 1
            if header & 0x80:
                pixels += data[offset:offset + size] * run
                offset += size
            else:
                pixels += data[offset:offset + run * size]
                offset += run * size
    rows = [pixels[y * width * size:(y + 1) * width * size] for y in range(height)]
    if not descriptor & 0x20:  # stored bottom-up
        rows.reverse()
    rgb = []
    for row in rows:
        rgb.append([(row[x + 2], row[x + 1], row[x]) for x in range(0, len(row), size)])
    return width, height, rgb


def encode_png(rows):
    """An RGB PNG of rows of RGB bytes."""
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    header = struct.pack('>IIBBBBB', len(rows[0]) // 3, len(rows), 8, 2, 0, 0, 0)
    raw = b''.join(b'\0' + bytes(row) for row in rows)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw, 9)) +
            chunk(b'IEND', b''))


def decode_png(data):
    """(width, rows of RGBA bytes) of an 8-bit RGB or RGBA PNG without interlacing."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG')
    offset, idat = 8, b''
    while offset < len(data):
        length, kind = struct.unpack_from('>I4s', data, offset)
        body = data[offset + 8:offset + 8 + length]
        if kind == b'IHDR':
            width, height, depth, colour, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif kind == b'IDAT':
            idat += body
        offset += 12 + length
    if depth != 8 or colour not in (2, 6) or interlace:
        raise ValueError(f'unsupported PNG (colour type {colour}, {depth} bits)')
    size = 4 if colour == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * size
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind, row = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = row[i - size] if i >= size else 0
            up, corner = previous[i], previous[i - size] if i >= size else 0
            if kind == 1:
                row[i] = (row[i] + left) & 0xFF
            elif kind == 2:
                row[i] = (row[i] + up) & 0xFF
            elif kind == 3:
                row[i] = (row[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                guess = left + up - corner
                pa, pb, pc = abs(guess - left), abs(guess - up), abs(guess - corner)
                row[i] = (row[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xFF
        rows.append(row)
        previous = row
    if size == 3:
        rows = [bytearray(b''.join(bytes(r[x:x + 3]) + b'\xff' for x in range(0, stride, 3))) for r in rows]
    return width, rows


def icon_rows(png_data):
    """An ICON_SIZE x ICON_SIZE PNG as rows of RGB bytes (alpha dropped), or None."""
    width, rows = decode_png(png_data)
    if width != ICON_SIZE or len(rows) != ICON_SIZE:
        return None
    return [bytearray(b''.join(bytes(r[x:x + 3]) for x in range(0, len(r), 4))) for r in rows]


def add_badge(rows, badge_png):
    """Draws a console badge (RGBA PNG) in the top-left corner of an icon's RGB rows."""
    width, badge = decode_png(badge_png)
    for y, source in enumerate(badge):
        target = rows[BADGE_MARGIN + y]
        for x in range(min(width, ICON_SIZE - 2 * BADGE_MARGIN)):
            r, g, b, a = source[4 * x:4 * x + 4]
            i = 3 * (BADGE_MARGIN + x)
            for c, value in enumerate((r, g, b)):
                target[i + c] = (value * a + target[i + c] * (255 - a)) // 255
    return rows


def remove_stale(client, remote, wanted):
    """Removes files a tile no longer has (from an earlier full copy), and their emptied folders."""
    stale = sorted(set(remote_files(client, remote)) - wanted)
    for relative in stale:
        delete(client, f'{remote}/{relative}')
    folders = {str(parent) for r in stale for parent in pathlib.PurePosixPath(r).parents} - {'.'}
    for folder in sorted(folders, key=lambda f: f.count('/'), reverse=True):
        try:
            client.rmd(f'{remote}/{folder}')
        except ftplib.Error:
            pass  # still holds files the tile keeps
    if stale:
        print(f'  removed {len(stale)} old files from {remote}')


def tile_param(param, tile_id, name):
    param = json.loads(json.dumps(param))
    number = tile_id[4:]
    param['titleId'] = tile_id
    param['conceptId'] = number
    param['contentId'] = f'UP9000-{tile_id}_00-PEGAMETILE{number}0'
    for key, value in param.get('localizedParameters', {}).items():
        if isinstance(value, dict):
            value['titleName'] = name
    return param


def clean_title(filename):
    """A game's name from its file's name, the way ProsperoEden makes one: without the extension
    and the tags dumps carry ("Name [0100...][v0]", "Name (USA)")."""
    stem = pathlib.PurePosixPath(filename).stem
    title = stem[7:] if stem.startswith('[Game] ') else stem
    tag = min((i for i in (title.find('['), title.find('(')) if i > 0), default=-1)
    if tag > 0:
        title = title[:tag]
    return title.rstrip(' _-') or stem


def fnv1a(text):
    """ProsperoEden's HashPath, which names its covers (covers/<hash>.tga) after the ROM's file name."""
    value = 2166136261
    for byte in text.encode():
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def installed(client, app_id):
    return bool(remote_files(client, f'{HOMEBREW}/{app_id}/sce_sys'))


def listed(client, folder, extensions):
    """The files directly in a console folder with one of the extensions, or None when it cannot be read."""
    try:
        client.cwd(folder)
        entries = list(client.mlsd())
    except ftplib.error_perm:
        return None
    finally:
        client.cwd('/')
    return sorted(name for name, facts in entries
                  if facts.get('type') == 'file' and pathlib.PurePosixPath(name).suffix.lower() in extensions)


def prosperoeden_games(client):
    """ProsperoEden's games: the ROMs in its game files folder, with the name and cover it keeps."""
    settings = read_remote(client, f'{CONFIG}/prosperoeden.json')
    try:
        folder = json.loads(settings).get('game_files') if settings else None
    except ValueError:
        folder = None
    files = listed(client, f'{folder or PROSPEROEDEN_DATA}/roms', EMULATORS[PROSPEROEDEN]['extensions'])
    if files is None:
        return None
    games = []
    for file in files:
        stem = f'{PROSPEROEDEN_DATA}/covers/{fnv1a(file):08x}'
        name = (read_remote(client, f'{stem}.name') or b'').decode('utf-8', 'replace').strip() or clean_title(file)
        title_id = re.search(r'\[(0[0-9A-Fa-f]{15})\]', file)
        games.append({'emulator': PROSPEROEDEN, 'file': file, 'name': name, 'cover': f'{stem}.tga',
                      'title_id': title_id.group(1).upper() if title_id else '',
                      'args': ['--rom', file, '--exit-after-game']})
    return games


def mupen64plus_games(client):
    """Mupen64Plus's games: the ROMs in its roms folder, with the cover it downloaded."""
    emulator = EMULATORS[MUPEN64PLUS]
    files = listed(client, emulator['roms'], emulator['extensions'])
    if files is None:
        return None
    return [{'emulator': MUPEN64PLUS, 'file': file, 'name': clean_title(file), 'title_id': '',
             'cover': f'{emulator["covers"]}/{pathlib.PurePosixPath(file).stem}.png',
             'args': [f'{emulator["roms"]}/{file}']} for file in files]


GAME_LISTS = {PROSPEROEDEN: prosperoeden_games, MUPEN64PLUS: mupen64plus_games}


def scaled_rows(width, height, pixel):
    """ICON_SIZE rows of RGB bytes from a picture of any size, pixel(x, y) giving its (r, g, b)."""
    rows = []
    for y in range(ICON_SIZE):
        source_y = y * height // ICON_SIZE
        rows.append(bytearray(b''.join(bytes(pixel(x * width // ICON_SIZE, source_y)) for x in range(ICON_SIZE))))
    return rows


def cover_rows(data):
    """A TGA or PNG cover as ICON_SIZE rows of RGB bytes."""
    if data[:8] == b'\x89PNG\r\n\x1a\n':
        width, rgba = decode_png(data)
        return scaled_rows(width, len(rgba), lambda x, y: rgba[y][4 * x:4 * x + 3])
    width, height, rgb = read_tga(data)
    return scaled_rows(width, height, lambda x, y: rgb[y][x])


def remove_tile(client, entry):
    """Deletes a tile's app folder; the home screen keeps its tile until it is deleted there too."""
    remote = f'{HOMEBREW}/{entry["tile"]}'
    for relative in remote_files(client, remote):
        delete(client, f'{remote}/{relative}')
    for folder in ('sce_sys', 'sce_module', ''):
        try:
            client.rmd(f'{remote}/{folder}'.rstrip('/'))
        except ftplib.Error:
            pass
    print(f'  removed {remote} ({entry.get("name", "")}); delete its tile on the home screen too (Options > Delete)')


def assign_tiles(games, tiles):
    """Keeps each game's tile (matched by emulator and title ID, then file name) and numbers new ones."""
    used = {entry['tile'] for entry in tiles}
    result = []
    for game in games:
        mine = [e for e in tiles if e.get('emulator', PROSPEROEDEN) == game['emulator']]
        entry = next((e for e in mine if game['title_id'] and e.get('title_id') == game['title_id']), None) or \
            next((e for e in mine if e.get('file') == game['file']), None)
        if entry is None:
            number = next((n for n in range(FIRST_TILE, LAST_TILE + 1) if f'PPSA{n}' not in used), None)
            if number is None:
                sys.exit('All tile IDs PPSA98001-PPSA98999 are in use')
            entry = {'tile': f'PPSA{number}'}
            used.add(entry['tile'])
            tiles.append(entry)
        entry.update(emulator=game['emulator'], file=game['file'], title_id=game['title_id'], name=game['name'])
        result.append((entry['tile'], game))
    return result


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument('host')
    parser.add_argument('app', type=pathlib.Path, help='the tile starter folder')
    parser.add_argument('--port', type=int, default=2121)
    parser.add_argument('--only', help='only the games whose name contains this text')
    parser.add_argument('--dry-run', action='store_true', help='list the tiles without changing the console')
    parser.add_argument('--label', choices=('badge', 'text', 'none'), default='none',
                        help="how a tile shows its game's console: not at all (default), a badge on its picture, or"
                        " text in front of its name")
    parser.add_argument('--console', help="console of every game (e.g. switch, ps2, pc), instead of each game's own")
    parser.add_argument('--force', action='store_true', help='copy tiles again even when they are up to date')
    parser.add_argument('--art', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent / 'art',
                        help="folder of pictures named like the games (\"Super Mario 64.png\") to use instead of"
                        " their covers (default: art next to this script)")
    parser.add_argument('--prune', action='store_true',
                        help='remove the tiles of games that are no longer on the console')
    parser.add_argument('--remove', metavar='NAME', help='remove the tiles whose name contains this text')
    options = parser.parse_args(argv)
    if options.console is not None and not console_key(options.console):
        sys.exit(f'Unknown console {options.console!r}; known: {", ".join(CONSOLES)}')
    app = options.app
    if not (app / 'eboot.bin').is_file() or not (app / 'sce_sys/param.json').is_file():
        sys.exit(f'{app} is not a tile starter folder')
    param = json.loads((app / 'sce_sys/param.json').read_text())
    files = sorted(p for p in app.rglob('*') if p.is_file() and p.relative_to(app).as_posix() not in SKIPPED | {LAUNCHER})
    # tile.txt names the starter too, so tiles of an older starter are copied again.
    starter = zlib.crc32((app / 'eboot.bin').read_bytes()) & 0xffffffff
    default_icon = (app / 'sce_sys/icon0.png').read_bytes() if (app / 'sce_sys/icon0.png').is_file() else None
    wanted = {p.relative_to(app).as_posix() for p in files} | {'tile.txt'}

    with connect(options.host, options.port) as client:
        stored = read_remote(client, f'{CONFIG}/tiles.json')
        tiles = json.loads(stored).get('tiles', []) if stored else []

        if options.remove is not None:
            gone = [e for e in tiles if options.remove.lower() in e.get('name', '').lower()]
            for entry in gone:
                print(f'{entry["tile"]}  {entry.get("name", "")}')
            if options.dry_run or not gone:
                return
            for entry in gone:
                remove_tile(client, entry)
                tiles.remove(entry)
            write_remote(client, f'{CONFIG}/tiles.json', (json.dumps({'tiles': tiles}, indent=2) + '\n').encode())
            return

        games, read = [], set()
        for emulator, game_list in GAME_LISTS.items():
            if not installed(client, emulator):
                continue
            found = game_list(client)
            if found is None:
                print(f'  could not read the games of {EMULATORS[emulator]["name"]}; its tiles are kept')
                continue
            read.add(emulator)
            games += found
        if options.only:
            games = [g for g in games if options.only.lower() in g['name'].lower()]
        plan = assign_tiles(games, tiles)
        planned = {tile_id for tile_id, _ in plan}
        # Only games of an emulator whose games were read count as gone.
        stale = [] if options.only else \
            [e for e in tiles if e['tile'] not in planned and e.get('emulator', PROSPEROEDEN) in read]
        for tile_id, game in plan:
            print(f'{tile_id}  {game["name"]}  ({EMULATORS[game["emulator"]]["name"]}: {game["file"]})')
        for entry in stale:
            print(f'{entry["tile"]}  {entry.get("name", "")}: game no longer there'
                  + ('' if options.prune else ' (--prune removes its tile)'))
        if options.dry_run or not (plan or (options.prune and stale)):
            return
        restore_icon(client, PROSPEROEDEN)  # an earlier version put a console label on it
        if (app / LAUNCHER).is_file():
            launcher = (app / LAUNCHER).read_bytes()
            if read_remote(client, f'{PROSPEROEDEN_DATA}/{LAUNCHER}') != launcher:
                ensure_directory(client, PROSPEROEDEN_DATA)
                write_remote(client, f'{PROSPEROEDEN_DATA}/{LAUNCHER}', launcher)
                print(f'  installed the tile launcher')

        for tile_id, game in plan:
            remote = f'{HOMEBREW}/{tile_id}'
            key = console_key(options.console) if options.console else game_console(game, game['emulator'])
            # tiles.json names each game's console too, for dashboards that show it.
            next(e for e in tiles if e['tile'] == tile_id)['console'] = key or ''
            name = game['name']
            if options.label == 'text' and key:
                name = f'[{CONSOLES[key]}] {name}'
            badge = None
            if options.label == 'badge' and key:
                if (BADGES / f'{key}.png').is_file():
                    badge = (BADGES / f'{key}.png').read_bytes()
                else:
                    print(f'  no badge for {key} in {BADGES}')
            art = options.art / f'{game["name"]}.png'
            art = art.read_bytes() if art.is_file() else None
            marks = ''.join(f'{mark}={zlib.crc32(data) & 0xffffffff:08x}\n'
                            for mark, data in (('badge', badge), ('art', art)) if data)
            arguments = ''.join(f'arg={argument}\n' for argument in game['args'])
            tile_text = f'app={game["emulator"]}\n{arguments}name={name}\n{marks}starter={starter:08x}\n'.encode()
            param_data = (json.dumps(tile_param(param, tile_id, name), indent=2) + '\n').encode()
            # Run on a schedule, this only copies new games' tiles and tiles of an updated app.
            if not options.force and read_remote(client, f'{remote}/tile.txt') == tile_text:
                remove_stale(client, remote, wanted)
                sync_copies(client, tile_id, param_data, read_remote(client, f'{remote}/sce_sys/icon0.png'))
                print(f'  {tile_id} is up to date')
                continue
            rows = None
            cover = art or (read_remote(client, game['cover']) if game.get('cover') else None)
            if cover:
                try:
                    rows = cover_rows(cover)
                except (ValueError, IndexError, struct.error, zlib.error) as error:
                    print(f'  cover not used: {error}')
            if rows is None and badge and default_icon:
                try:
                    rows = icon_rows(default_icon)
                except (ValueError, IndexError, struct.error, zlib.error) as error:
                    print(f'  no badge on the default picture: {error}')
            if rows is not None and badge:
                try:
                    rows = add_badge(rows, badge)
                except (ValueError, IndexError, struct.error, zlib.error) as error:
                    print(f'  badge not used: {error}')
            icon = encode_png(rows) if rows is not None else None
            for local in files:
                relative = local.relative_to(app).as_posix()
                ensure_directory(client, f'{remote}/{relative}'.rsplit('/', 1)[0])
                if relative == 'sce_sys/param.json':
                    data = param_data
                elif relative == 'sce_sys/icon0.png' and icon:
                    data = icon
                else:
                    data = local.read_bytes()
                write_remote(client, f'{remote}/{relative}', data)
            write_remote(client, f'{remote}/tile.txt', tile_text)
            sync_copies(client, tile_id, param_data, icon or default_icon)
            remove_stale(client, remote, wanted)
            print(f'  installed {remote}')
        if options.prune:
            for entry in stale:
                remove_tile(client, entry)
                tiles.remove(entry)
        write_remote(client, f'{CONFIG}/tiles.json', (json.dumps({'tiles': tiles}, indent=2) + '\n').encode())
    print(f'{len(plan)} tiles. Restart the PS5 (or your homebrew loader) if new ones do not appear.')


if __name__ == '__main__':
    main(sys.argv[1:])

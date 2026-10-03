#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Makes a PS5 home screen tile for each game in ProsperoEden's Library, over FTP.

  tools/game-tiles/make-tiles.py HOST APP_DIR [--port 2121] [--only NAME] [--dry-run]

APP_DIR is the PPSA99008 folder of a release ZIP with game tiles (headless/game_tile.h). Each tile
is a copy of it installed as /data/homebrew/PPSA98001-PPSA98999 with the game's name and cover in
its sce_sys and a tile.txt that names the game, so the PS5 shows that game while it runs.

The games come from /data/prosperoeden/config/library.json, which the Library writes: open the
Library once after adding games. Which tile belongs to which game is kept in
/data/prosperoeden/config/tiles.json, so running this again updates the same tiles and only adds
new ones; a tile that is already up to date is skipped, so it can run on a schedule. A tile is a full copy of the app (about 80 MB). Needs a running FTP server on the
console (the Payload SDK's ftpsrv, port 2121) and Python 3 only.
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


def connect(host, port):
    client = ftplib.FTP()
    client.connect(host, port, timeout=60)
    client.login()
    client.voidcmd('TYPE I')  # binary, which SIZE needs
    return client


def read_remote(client, path):
    buffer = io.BytesIO()
    try:
        client.retrbinary(f'RETR {path}', buffer.write)
    except ftplib.error_perm:
        return None
    return buffer.getvalue()


def write_remote(client, path, data):
    client.storbinary(f'STOR {path}.partial', io.BytesIO(data))
    try:
        client.delete(path)
    except ftplib.error_perm:
        pass
    client.rename(f'{path}.partial', path)


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


def png_icon(tga_data):
    width, height, rgb = read_tga(tga_data)
    raw = bytearray()
    for y in range(ICON_SIZE):
        source = rgb[y * height // ICON_SIZE]
        raw.append(0)
        for x in range(ICON_SIZE):
            raw += bytes(source[x * width // ICON_SIZE])

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', ICON_SIZE, ICON_SIZE, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


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


def assign_tiles(games, tiles):
    """Keeps each game's tile (matched by title ID, then file name) and numbers new ones."""
    used = {entry['tile'] for entry in tiles}
    result = []
    for game in games:
        entry = next((e for e in tiles if game['title_id'] and e.get('title_id') == game['title_id']), None) or \
            next((e for e in tiles if e.get('file') == game['file']), None)
        if entry is None:
            number = next((n for n in range(FIRST_TILE, LAST_TILE + 1) if f'PPSA{n}' not in used), None)
            if number is None:
                sys.exit('All tile IDs PPSA98001-PPSA98999 are in use')
            entry = {'tile': f'PPSA{number}'}
            used.add(entry['tile'])
            tiles.append(entry)
        entry.update(file=game['file'], title_id=game['title_id'], name=game['name'])
        result.append((entry['tile'], game))
    return result


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument('host')
    parser.add_argument('app', type=pathlib.Path)
    parser.add_argument('--port', type=int, default=2121)
    parser.add_argument('--only', help='only the games whose name contains this text')
    parser.add_argument('--dry-run', action='store_true', help='list the tiles without changing the console')
    parser.add_argument('--force', action='store_true', help='copy tiles again even when they are up to date')
    options = parser.parse_args(argv)
    app = options.app
    if not (app / 'eboot.bin').is_file() or not (app / 'sce_sys/param.json').is_file():
        sys.exit(f'{app} is not a PPSA99008 folder from a release ZIP')
    param = json.loads((app / 'sce_sys/param.json').read_text())
    files = sorted(p for p in app.rglob('*') if p.is_file())

    with connect(options.host, options.port) as client:
        library = read_remote(client, f'{CONFIG}/library.json')
        if library is None:
            sys.exit('No library.json on the console yet: open the Library in ProsperoEden once')
        games = [g for g in json.loads(library).get('games', []) if g.get('file')]
        if options.only:
            games = [g for g in games if options.only.lower() in g.get('name', '').lower()]
        stored = read_remote(client, f'{CONFIG}/tiles.json')
        tiles = json.loads(stored).get('tiles', []) if stored else []
        plan = assign_tiles(games, tiles)
        for tile_id, game in plan:
            print(f'{tile_id}  {game["name"]}  ({game["file"]})')
        if options.dry_run or not plan:
            return

        for tile_id, game in plan:
            remote = f'{HOMEBREW}/{tile_id}'
            title = f'title={game["title_id"]}\n' if re.fullmatch(r'[0-9A-Fa-f]{16}', game.get('title_id', '')) else ''
            tile_text = f'rom={game["file"]}\n{title}'.encode()
            # Run on a schedule, this only copies new games' tiles and tiles of an updated app.
            if not options.force and read_remote(client, f'{remote}/tile.txt') == tile_text:
                try:
                    current = client.size(f'{remote}/eboot.bin') == (app / 'eboot.bin').stat().st_size
                except ftplib.error_perm:
                    current = False
                if current:
                    print(f'  {tile_id} is up to date')
                    continue
            icon = None
            if game.get('cover'):
                cover = read_remote(client, game['cover'])
                try:
                    icon = png_icon(cover) if cover else None
                except (ValueError, IndexError, struct.error) as error:
                    print(f'  cover not used: {error}')
            for local in files:
                relative = local.relative_to(app).as_posix()
                ensure_directory(client, f'{remote}/{relative}'.rsplit('/', 1)[0])
                if relative == 'sce_sys/param.json':
                    data = (json.dumps(tile_param(param, tile_id, game['name']), indent=2) + '\n').encode()
                elif relative == 'sce_sys/icon0.png' and icon:
                    data = icon
                else:
                    data = local.read_bytes()
                write_remote(client, f'{remote}/{relative}', data)
            write_remote(client, f'{remote}/tile.txt', tile_text)
            print(f'  installed {remote}')
        write_remote(client, f'{CONFIG}/tiles.json', (json.dumps({'tiles': tiles}, indent=2) + '\n').encode())
    print(f'{len(plan)} tiles. Restart the PS5 (or your homebrew loader) if new ones do not appear.')


if __name__ == '__main__':
    main(sys.argv[1:])

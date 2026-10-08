#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the console badges make-tiles.py puts in the top-left corner of each game's tile.

  tools/game-tiles/badges/make-badges.py [FONT.ttf]

Writes <console>.png (RGBA, 72 pixels high, for a 512x512 icon) next to this script for every
console in make-tiles.py's CONSOLES: the console's name in white on its maker's colour, in a
rounded pill like the home screen's own labels. Needs Pillow; make-tiles.py itself does not.
The default font is Liberation Sans Bold (SIL Open Font License).
"""
import importlib.util
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = pathlib.Path(__file__).resolve().parent
FONT = '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf'
HEIGHT, SCALE = 72, 4
NINTENDO, SONY, SEGA, MICROSOFT = (230, 0, 18), (0, 55, 145), (0, 102, 204), (16, 124, 16)
COLOURS = {
    'switch': NINTENDO, '3ds': NINTENDO, 'ds': NINTENDO, 'gba': (75, 0, 130), 'gbc': (75, 0, 130),
    'gb': (75, 0, 130), 'gamecube': (106, 90, 205), 'wii': (0, 154, 199), 'wiiu': (0, 154, 199),
    'n64': NINTENDO, 'snes': NINTENDO, 'nes': NINTENDO, 'ps1': SONY, 'ps2': SONY, 'ps3': SONY,
    'psp': SONY, 'vita': SONY, 'dreamcast': (240, 100, 0), 'saturn': SEGA, 'megadrive': SEGA,
    'mastersystem': SEGA, 'xbox': MICROSOFT, 'xbox360': MICROSOFT, 'arcade': (200, 140, 0),
    'pc': (60, 64, 72),
}
# Badge text where the console's usual short name differs from the name under the tile.
TEXT = {'gbc': 'GB COLOR', 'gb': 'GAME BOY', 'wii': 'Wii', 'wiiu': 'Wii U', 'vita': 'PS VITA',
        'megadrive': 'MEGA DRIVE', 'mastersystem': 'MASTER SYS'}


def consoles():
    spec = importlib.util.spec_from_file_location('make_tiles', HERE.parent / 'make-tiles.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.CONSOLES


def badge(text, colour, font_path):
    height = HEIGHT * SCALE
    font = ImageFont.truetype(font_path, int(height * 0.6))
    measure = ImageDraw.Draw(Image.new('L', (1, 1)))
    left, top, right, bottom = measure.textbbox((0, 0), text, font=font)
    width = right - left + int(height * 0.8)
    image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([0, 0, width - 1, height - 1], radius=height // 4, fill=colour + (255,),
                           outline=(255, 255, 255, 255), width=3 * SCALE)
    draw.text(((width - (right - left)) // 2 - left, (height - (bottom - top)) // 2 - top), text, font=font,
              fill=(255, 255, 255, 255))
    return image.resize((width // SCALE, HEIGHT), Image.LANCZOS)


def main(argv):
    font = argv[0] if argv else FONT
    for key, name in consoles().items():
        badge(TEXT.get(key, name.upper()), COLOURS[key], font).save(HERE / f'{key}.png', optimize=True)
    print(f'Wrote {len(consoles())} badges to {HERE}')


if __name__ == '__main__':
    main(sys.argv[1:])

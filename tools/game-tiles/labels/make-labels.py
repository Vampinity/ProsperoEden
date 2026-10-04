#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the console label icons (512x512 PNG) that make-tiles.py puts on emulator apps.

  tools/game-tiles/labels/make-labels.py [FONT.ttf]

Plain bold text on a console colour, like the PS5's own tile: not the consoles' logos. Needs
Pillow; the default font is Liberation Sans Bold (SIL Open Font License).
"""
import pathlib
import sys

from PIL import Image, ImageDraw, ImageFont

# slug: (lines, background, text)
LABELS = {
    'switch': (('NINTENDO', 'SWITCH'), '#E60012', '#FFFFFF'),
    '3ds': (('NINTENDO', '3DS'), '#CE181E', '#FFFFFF'),
    'ds': (('NINTENDO', 'DS'), '#5A5A5F', '#FFFFFF'),
    'gba': (('GAME BOY', 'ADVANCE'), '#4B2E83', '#FFFFFF'),
    'gamecube-wii': (('GAMECUBE', 'WII'), '#4B4B9B', '#FFFFFF'),
    'n64': (('NINTENDO', '64'), '#00954B', '#FFFFFF'),
    'snes': (('SUPER', 'NINTENDO'), '#514689', '#FFFFFF'),
    'nes': (('NINTENDO', 'NES'), '#B8282E', '#FFFFFF'),
    'ps1': (('PLAYSTATION',), '#8A8D93', '#FFFFFF'),
    'ps2': (('PLAYSTATION', '2'), '#1A1A6B', '#FFFFFF'),
    'ps3': (('PLAYSTATION', '3'), '#111111', '#FFFFFF'),
    'psp': (('PLAYSTATION', 'PORTABLE'), '#2A2A2E', '#FFFFFF'),
    'vita': (('PLAYSTATION', 'VITA'), '#1F4EA1', '#FFFFFF'),
    'dreamcast': (('SEGA', 'DREAMCAST'), '#F37021', '#FFFFFF'),
    'saturn': (('SEGA', 'SATURN'), '#2B3A8C', '#FFFFFF'),
    'megadrive': (('SEGA', 'MEGA DRIVE'), '#1B1B1B', '#FFFFFF'),
    'xbox': (('XBOX',), '#107C10', '#FFFFFF'),
    'xbox360': (('XBOX', '360'), '#5DC21E', '#FFFFFF'),
    'arcade': (('ARCADE',), '#8B0000', '#FFFFFF'),
}
SIZE, SCALE, WIDTH, GAP = 512, 4, 380, 34


def draw(lines, background, text, font_path):
    side = SIZE * SCALE
    image = Image.new('RGB', (side, side), background)
    pen = ImageDraw.Draw(image)
    fitted = []
    for line in lines:
        size = 400
        while True:
            font = ImageFont.truetype(font_path, size)
            box = pen.textbbox((0, 0), line, font=font)
            if box[2] - box[0] <= WIDTH * SCALE and box[3] - box[1] <= 150 * SCALE:
                break
            size -= 4
        fitted.append((line, font, box))
    height = sum(b[3] - b[1] for _, _, b in fitted) + GAP * SCALE * (len(fitted) - 1)
    y = (side - height) // 2
    for line, font, box in fitted:
        pen.text(((side - (box[2] - box[0])) // 2 - box[0], y - box[1]), line, font=font, fill=text)
        y += box[3] - box[1] + GAP * SCALE
    return image.resize((SIZE, SIZE), Image.LANCZOS)


def main(argv):
    font = argv[0] if argv else '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf'
    here = pathlib.Path(__file__).resolve().parent
    for slug, (lines, background, text) in LABELS.items():
        draw(lines, background, text, font).save(here / f'{slug}.png', optimize=True)
        print(f'{slug}.png')


if __name__ == '__main__':
    main(sys.argv[1:])

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the game tile starter (main.cpp) with the PS5 Native App Boilerplate and writes
# dist/tile-starter/ (eboot.bin, sce_module/libc.prx, sce_sys/param.json, sce_sys/icon0.png) and
# dist/tile-starter.zip, which tools/game-tiles/make-tiles.py installs as each game's tile, and
# the tile launcher payload (../launcher/tile-launch.c) as dist/tile-starter/tile-launch.elf.
# Needs what the boilerplate needs (Clang 18, lld 18, make, ninja, wget, unzip).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
root=$(cd -- "$here/../../.." && pwd)
work=${TILE_STARTER_WORK:-$root/build/tile-starter}
commit=b1315a976c320d57f5dc89699106c3cdf957abb7
if [[ ! -d $work/.git ]]; then
    git clone -q https://github.com/blackbearreloaded/ps5-native-app-boilerplate.git "$work"
fi
git -C "$work" fetch -q origin "$commit" && git -C "$work" checkout -q "$commit"
cp "$here/main.cpp" "$work/src/main.cpp"
make -C "$work" USE_CCACHE=0
out=$root/dist/tile-starter
rm -rf "$out" && mkdir -p "$out/sce_module" "$out/sce_sys"
cp "$work/dist/PPSA99999/eboot.bin" "$out/"
cp "$work/dist/PPSA99999/sce_module/libc.prx" "$out/sce_module/"
cp "$work/dist/PPSA99999/sce_sys/param.json" "$work/dist/PPSA99999/sce_sys/icon0.png" "$out/sce_sys/"
sdk=$work/.deps/native/ps5-payload-sdk
"$sdk/bin/prospero-clang" -Wall -Werror -O2 -o "$out/tile-launch.elf" "$here/../launcher/tile-launch.c" \
    -lkernel_sys -lSceSystemService -lSceUserService
(cd "$root/dist" && rm -f tile-starter.zip && zip -qr tile-starter.zip tile-starter)
echo "Built $out"

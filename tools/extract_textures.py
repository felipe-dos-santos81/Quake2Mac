#!/usr/bin/env python3
"""Extract Quake II world textures from pak files to PNG.

Reads pak0.pak … pak9.pak in a game directory (later paks win, as in the
engine), maps every textures/**/*.wal through the palette in
pics/colormap.pcx, and writes <out>/textures/<dir>/<name>.png (lowercase).
Palette index 255 becomes alpha 0. Existing files are skipped unless
--force is given.

Run with:  uv run --project tools tools/extract_textures.py --gamedir baseq2
"""
from __future__ import annotations

import io
from pathlib import Path

from PIL import Image
from vgio.quake2 import pak

COLORMAP = "pics/colormap.pcx"
TRANSPARENT = 255  # palette index the engine treats as fully transparent


def open_paks(gamedir: Path) -> list[pak.PakFile]:
    """pak0.pak … pak9.pak that exist in gamedir, in numeric order (engine precedence order)."""
    paks: list[pak.PakFile] = []
    for i in range(10):
        p = gamedir / f"pak{i}.pak"
        if p.is_file():
            paks.append(pak.PakFile(str(p)))
    return paks


def build_index(paks: list[pak.PakFile]) -> dict[str, pak.PakFile]:
    """Entry name -> the pak that wins for it. Later paks override earlier ones."""
    index: dict[str, pak.PakFile] = {}
    for pf in paks:
        for name in pf.namelist():
            index[name] = pf
    return index


def load_palette(index: dict[str, pak.PakFile]) -> list[int]:
    """768 ints (r, g, b per index) from pics/colormap.pcx."""
    pf = index.get(COLORMAP)
    if pf is None:
        raise FileNotFoundError(f"{COLORMAP} not found in any pak")
    im = Image.open(io.BytesIO(pf.read(COLORMAP)))
    im.load()
    if im.mode == "L":  # Pillow reads a linear gray palette back as "L"; rebuild it
        return [v for i in range(256) for v in (i, i, i)]
    palette = im.getpalette()
    if palette is None:
        raise ValueError(f"{COLORMAP} has no palette")
    return palette[:768]


# alpha byte per palette index: 0 for the transparent index, 255 otherwise
_ALPHA_TABLE = bytes(0 if i == TRANSPARENT else 255 for i in range(256))


def _neighbour(data: bytes, i: int, width: int) -> int:
    """Palette index of an adjacent opaque texel, in the engine's order (GL_Upload8):
    above, below, left, right. Falls back to 0. Mirrors ref_gl/gl_image.c so the
    colour under transparent texels matches what the engine would have used."""
    s = len(data)
    if i > width and data[i - width] != TRANSPARENT:
        return data[i - width]
    if i < s - width and data[i + width] != TRANSPARENT:
        return data[i + width]
    if i > 0 and data[i - 1] != TRANSPARENT:
        return data[i - 1]
    if i < s - 1 and data[i + 1] != TRANSPARENT:
        return data[i + 1]
    return 0


def wal_to_image(mip0: bytes, width: int, height: int, palette: list[int]) -> Image.Image:
    """RGB when every texel is opaque; RGBA otherwise (index 255 -> alpha 0, colour from a neighbour)."""
    if len(mip0) != width * height:
        raise ValueError(f"expected {width * height} texels, got {len(mip0)}")
    if TRANSPARENT not in mip0:
        indexed = Image.frombytes("P", (width, height), mip0)
        indexed.putpalette(palette)
        return indexed.convert("RGB")
    filled = bytearray(mip0)
    for i, p in enumerate(mip0):
        if p == TRANSPARENT:
            filled[i] = _neighbour(mip0, i, width)
    indexed = Image.frombytes("P", (width, height), bytes(filled))
    indexed.putpalette(palette)
    out = indexed.convert("RGB")
    out.putalpha(Image.frombytes("L", (width, height), mip0.translate(_ALPHA_TABLE)))
    return out

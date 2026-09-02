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

import argparse
import io
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image
from vgio.quake2 import pak, wal

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


@dataclass
class Summary:
    extracted: int = 0
    skipped: int = 0
    failed: int = 0


def dest_path(out: Path, pak_name: str) -> Path:
    """textures/e1u1/PIP04_4.wal -> <out>/textures/e1u1/pip04_4.png (the renderer probes lowercase)."""
    return out / Path(pak_name.lower()).with_suffix(".png")


def _is_world_texture(name: str) -> bool:
    return name.startswith("textures/") and name.lower().endswith(".wal")


def extract(gamedir: Path, out: Path, force: bool = False) -> Summary:
    """Write every textures/**/*.wal in gamedir's paks as PNG under out/textures/."""
    paks = open_paks(gamedir)
    if not paks:
        raise FileNotFoundError(f"no pak0.pak … pak9.pak in {gamedir}")
    try:
        index = build_index(paks)
        palette = load_palette(index)
        summary = Summary()
        for name in sorted(n for n in index if _is_world_texture(n)):
            dest = dest_path(out, name)
            if dest.exists() and not force:
                summary.skipped += 1
                continue
            try:
                w = wal.Wal.open(io.BytesIO(index[name].read(name)))
                img = wal_to_image(w.pixels[: w.width * w.height], w.width, w.height, palette)
            except Exception as exc:  # one bad texture must not stop the run
                print(f"FAILED {name}: {type(exc).__name__}: {exc}", file=sys.stderr)
                summary.failed += 1
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            img.save(dest)
            summary.extracted += 1
        return summary
    finally:
        for pf in paks:
            pf.close()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--gamedir", type=Path, default=Path("baseq2"),
        help="directory holding pak0.pak … pak9.pak (default: baseq2)",
    )
    parser.add_argument(
        "--out", type=Path, default=None,
        help="output root; PNGs land in <out>/textures (default: same as --gamedir)",
    )
    parser.add_argument(
        "--force", action="store_true",
        help="overwrite existing files instead of skipping them",
    )
    args = parser.parse_args(argv)
    out = args.out if args.out is not None else args.gamedir
    try:
        summary = extract(args.gamedir, out, args.force)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    print(f"extracted {summary.extracted}, skipped {summary.skipped}, failed {summary.failed}")
    return 1 if summary.failed else 0


if __name__ == "__main__":
    sys.exit(main())

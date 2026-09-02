"""Tests for tools/extract_textures.py against synthetic paks built in tmp_path."""
from __future__ import annotations

import io
import struct
from pathlib import Path

import pytest
from PIL import Image
from vgio.quake2 import pak

import extract_textures

# name[32] width height offsets[4] animname[32] flags contents value == 100 bytes
WAL_HEADER = struct.Struct("<32sII4I32siii")


def make_palette() -> list[int]:
    """A non-grayscale 256-entry palette. (Pillow reads a linear gray PCX back as mode "L".)"""
    pal: list[int] = []
    for i in range(256):
        pal += [i, (i * 7) % 256, 255 - i]
    return pal


def rgb(palette: list[int], index: int) -> tuple[int, int, int]:
    return tuple(palette[index * 3 : index * 3 + 3])  # type: ignore[return-value]


def make_pcx(palette: list[int]) -> bytes:
    im = Image.new("P", (16, 16))
    im.putpalette(palette)
    buf = io.BytesIO()
    im.save(buf, format="PCX")
    return buf.getvalue()


def make_wal(name: str, width: int, height: int, mip0: bytes) -> bytes:
    """A .wal with the given mip 0 and zero-filled mips 1-3 (vgio reads w*h*85//64 texels)."""
    assert len(mip0) == width * height
    total = width * height * 85 // 64
    filler = bytes(total - len(mip0))
    offsets = (
        WAL_HEADER.size,
        WAL_HEADER.size + width * height,
        WAL_HEADER.size + width * height + width * height // 4,
        WAL_HEADER.size + width * height + width * height // 4 + width * height // 16,
    )
    header = WAL_HEADER.pack(name.encode("ascii"), width, height, *offsets, b"", 0, 0, 0)
    return header + mip0 + filler


def write_pak(path: Path, entries: dict[str, bytes]) -> None:
    with pak.PakFile(str(path), "w") as pf:
        for name, data in entries.items():
            pf.writestr(name, data)


@pytest.fixture
def palette() -> list[int]:
    return make_palette()


@pytest.fixture
def gamedir(tmp_path: Path, palette: list[int]) -> Path:
    d = tmp_path / "baseq2"
    d.mkdir()
    write_pak(
        d / "pak0.pak",
        {
            "pics/colormap.pcx": make_pcx(palette),
            "textures/e1u1/floor.wal": make_wal("e1u1/floor", 4, 2, bytes([1, 2, 3, 4, 5, 6, 7, 8])),
            "textures/e1u1/GRATE.wal": make_wal("e1u1/GRATE", 2, 2, bytes([9, 255, 255, 255])),
            "textures/e2u1/wall.wal": make_wal("e2u1/wall", 2, 2, bytes([10, 10, 10, 10])),
        },
    )
    write_pak(
        d / "pak1.pak",
        {"textures/e2u1/wall.wal": make_wal("e2u1/wall", 2, 2, bytes([20, 20, 20, 20]))},
    )
    return d


# --- Task 1: pak index and palette -------------------------------------------


def test_open_paks_finds_existing_paks_in_order(gamedir: Path) -> None:
    paks = extract_textures.open_paks(gamedir)
    assert len(paks) == 2
    assert [Path(p.fp.name).name for p in paks] == ["pak0.pak", "pak1.pak"]
    for p in paks:
        p.close()


def test_open_paks_empty_dir(tmp_path: Path) -> None:
    assert extract_textures.open_paks(tmp_path) == []


def test_index_later_pak_wins(gamedir: Path) -> None:
    index = extract_textures.build_index(extract_textures.open_paks(gamedir))
    data = index["textures/e2u1/wall.wal"].read("textures/e2u1/wall.wal")
    assert data[WAL_HEADER.size : WAL_HEADER.size + 4] == bytes([20, 20, 20, 20])


def test_index_keeps_entries_only_in_pak0(gamedir: Path) -> None:
    index = extract_textures.build_index(extract_textures.open_paks(gamedir))
    assert "textures/e1u1/floor.wal" in index
    assert "pics/colormap.pcx" in index


def test_load_palette(gamedir: Path, palette: list[int]) -> None:
    index = extract_textures.build_index(extract_textures.open_paks(gamedir))
    assert extract_textures.load_palette(index) == palette


def test_load_palette_missing_colormap_raises(tmp_path: Path) -> None:
    d = tmp_path / "nocolormap"
    d.mkdir()
    write_pak(d / "pak0.pak", {"textures/e1u1/x.wal": make_wal("e1u1/x", 2, 2, bytes(4))})
    index = extract_textures.build_index(extract_textures.open_paks(d))
    with pytest.raises(FileNotFoundError):
        extract_textures.load_palette(index)


# --- Task 2: wal_to_image ------------------------------------------------------


def test_opaque_wal_becomes_rgb(palette: list[int]) -> None:
    img = extract_textures.wal_to_image(bytes([1, 2, 3, 4]), 2, 2, palette)
    assert img.mode == "RGB"
    assert img.size == (2, 2)
    assert img.getpixel((0, 0)) == rgb(palette, 1)
    assert img.getpixel((1, 0)) == rgb(palette, 2)
    assert img.getpixel((0, 1)) == rgb(palette, 3)
    assert img.getpixel((1, 1)) == rgb(palette, 4)


def test_transparent_wal_becomes_rgba_with_neighbour_fill(palette: list[int]) -> None:
    # texel order: (0,0)=9 opaque, (1,0)=255, (0,1)=255, (1,1)=255
    img = extract_textures.wal_to_image(bytes([9, 255, 255, 255]), 2, 2, palette)
    assert img.mode == "RGBA"
    assert img.getpixel((0, 0)) == rgb(palette, 9) + (255,)
    # (1,0): above/below are 255 or out of range, left neighbour is 9 -> its colour, alpha 0
    assert img.getpixel((1, 0)) == rgb(palette, 9) + (0,)
    # (0,1) and (1,1): no opaque neighbour in the engine's order -> palette 0, alpha 0
    assert img.getpixel((0, 1)) == rgb(palette, 0) + (0,)
    assert img.getpixel((1, 1)) == rgb(palette, 0) + (0,)


def test_neighbour_prefers_texel_above(palette: list[int]) -> None:
    # 2 wide, 3 tall; texel index 4 (x=0,y=2) is 255; above it (index 2) is 7, left is out of row
    mip0 = bytes([1, 1, 7, 1, 255, 3])
    img = extract_textures.wal_to_image(mip0, 2, 3, palette)
    assert img.getpixel((0, 2)) == rgb(palette, 7) + (0,)


def test_wal_to_image_rejects_wrong_length(palette: list[int]) -> None:
    with pytest.raises(ValueError):
        extract_textures.wal_to_image(bytes(3), 2, 2, palette)


# --- Task 3: extract() and main() ----------------------------------------------


def png_names(root: Path) -> list[str]:
    return sorted(p.relative_to(root).as_posix() for p in root.rglob("*.png"))


def test_dest_path_lowercases_and_swaps_extension(tmp_path: Path) -> None:
    assert extract_textures.dest_path(tmp_path, "textures/e1u1/PIP04_4.wal") == (
        tmp_path / "textures" / "e1u1" / "pip04_4.png"
    )


def test_extract_writes_lowercase_pngs_mirroring_pak_layout(gamedir: Path) -> None:
    summary = extract_textures.extract(gamedir, gamedir)
    assert summary == extract_textures.Summary(extracted=3, skipped=0, failed=0)
    assert png_names(gamedir) == [
        "textures/e1u1/floor.png",
        "textures/e1u1/grate.png",
        "textures/e2u1/wall.png",
    ]


def test_extract_uses_mip0_and_palette(gamedir: Path, palette: list[int]) -> None:
    extract_textures.extract(gamedir, gamedir)
    floor = Image.open(gamedir / "textures/e1u1/floor.png")
    assert floor.mode == "RGB"
    assert floor.size == (4, 2)
    assert floor.getpixel((0, 0)) == rgb(palette, 1)
    assert floor.getpixel((3, 1)) == rgb(palette, 8)
    grate = Image.open(gamedir / "textures/e1u1/grate.png")
    assert grate.mode == "RGBA"
    assert grate.getpixel((1, 0)) == rgb(palette, 9) + (0,)


def test_extract_uses_winning_pak(gamedir: Path, palette: list[int]) -> None:
    extract_textures.extract(gamedir, gamedir)
    wall = Image.open(gamedir / "textures/e2u1/wall.png")
    assert wall.getpixel((0, 0)) == rgb(palette, 20)


def test_extract_skips_existing_unless_force(gamedir: Path) -> None:
    dest = gamedir / "textures/e1u1/floor.png"
    dest.parent.mkdir(parents=True)
    dest.write_bytes(b"keep me")
    summary = extract_textures.extract(gamedir, gamedir)
    assert summary == extract_textures.Summary(extracted=2, skipped=1, failed=0)
    assert dest.read_bytes() == b"keep me"
    summary = extract_textures.extract(gamedir, gamedir, force=True)
    assert summary == extract_textures.Summary(extracted=3, skipped=0, failed=0)
    assert dest.read_bytes() != b"keep me"
    assert Image.open(dest).size == (4, 2)


def test_extract_out_redirects_root(gamedir: Path, tmp_path: Path) -> None:
    out = tmp_path / "elsewhere"
    extract_textures.extract(gamedir, out)
    assert (out / "textures/e1u1/floor.png").is_file()
    assert not (gamedir / "textures").exists()


def test_extract_reports_corrupt_wal_and_continues(gamedir: Path, capsys: pytest.CaptureFixture[str]) -> None:
    write_pak(gamedir / "pak2.pak", {"textures/e1u1/bad.wal": b"garbage"})
    summary = extract_textures.extract(gamedir, gamedir)
    assert summary == extract_textures.Summary(extracted=3, skipped=0, failed=1)
    assert "FAILED textures/e1u1/bad.wal" in capsys.readouterr().err
    assert not (gamedir / "textures/e1u1/bad.png").exists()


def test_extract_without_paks_raises(tmp_path: Path) -> None:
    with pytest.raises(FileNotFoundError):
        extract_textures.extract(tmp_path, tmp_path)


def test_main_exit_codes_and_summary(gamedir: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert extract_textures.main(["--gamedir", str(gamedir)]) == 0
    assert "extracted 3, skipped 0, failed 0" in capsys.readouterr().out
    assert extract_textures.main(["--gamedir", str(gamedir)]) == 0
    assert "extracted 0, skipped 3, failed 0" in capsys.readouterr().out
    write_pak(gamedir / "pak2.pak", {"textures/e1u1/bad.wal": b"garbage"})
    assert extract_textures.main(["--gamedir", str(gamedir), "--force"]) == 1
    empty = tmp_path / "empty"
    empty.mkdir()
    assert extract_textures.main(["--gamedir", str(empty)]) == 2
    assert "error:" in capsys.readouterr().err


def test_main_out_option(gamedir: Path, tmp_path: Path) -> None:
    out = tmp_path / "o"
    assert extract_textures.main(["--gamedir", str(gamedir), "--out", str(out)]) == 0
    assert (out / "textures/e2u1/wall.png").is_file()

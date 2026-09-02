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

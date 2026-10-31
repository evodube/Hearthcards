# Hearthcards - collect trading cards and pin them to a poster in Skyrim Special Edition.
# Copyright (C) 2026 evodube
#
# This program is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
# even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program (LICENSE.txt).
# If not, see <https://www.gnu.org/licenses/>.

"""Writes BC1 (DXT1) / BC3 (DXT5) DDS textures with a full mipmap chain, using Pillow's block encoder."""
import io
import struct

from PIL import Image


def _level(img, fourcc):
    b = io.BytesIO()
    img.save(b, format="DDS", pixel_format=fourcc)
    return b.getvalue()[128:]


def save(img, path, alpha=False):
    """img: a PIL image whose sides are powers of two (>= 4)."""
    fourcc = "DXT5" if alpha else "DXT1"
    img = img.convert("RGBA" if alpha else "RGB")
    w, h = img.size
    levels = []
    cur = img
    while True:
        levels.append(_level(cur, fourcc))
        if cur.size[0] <= 4 or cur.size[1] <= 4:
            break
        cur = cur.resize((max(4, cur.size[0] // 2), max(4, cur.size[1] // 2)), Image.LANCZOS)
    block = 16 if alpha else 8
    linear = max(1, (w + 3) // 4) * max(1, (h + 3) // 4) * block
    # DDSD_CAPS | HEIGHT | WIDTH | PIXELFORMAT | MIPMAPCOUNT | LINEARSIZE
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000
    hdr = b"DDS " + struct.pack("<7I", 124, flags, h, w, linear, 0, len(levels)) + b"\0" * 44
    hdr += struct.pack("<2I4s5I", 32, 0x4, fourcc.encode(), 0, 0, 0, 0, 0)  # pixel format: FOURCC
    hdr += struct.pack("<5I", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)            # TEXTURE | MIPMAP | COMPLEX
    assert len(hdr) == 128
    with open(path, "wb") as f:
        f.write(hdr + b"".join(levels))

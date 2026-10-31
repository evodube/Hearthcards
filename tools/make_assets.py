#!/usr/bin/env python3
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

"""Builds every texture and mesh Hearthcards needs.

  python3 tools/make_assets.py [output Data folder]   (default: build/Data)

Card art: put art/cards/001.png ... art/cards/100.png (png/jpg/webp, any size; cropped to 5:7 portrait).
A card without art gets a placeholder (number, name and rarity frame). Names and rarities: tools/cards.csv.
Optional: art/back.png (card back) and art/poster.png (poster background, 80:106 portrait; slots are drawn on top).
"""
import csv
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dds  # noqa: E402
import nif  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "tools", "fonts")
ART = os.path.join(ROOT, "art")

CARD_W, CARD_H = 6.0, 8.4           # game units (1 unit ~ 1.43 cm)
POSTER_W, POSTER_H = 80.0, 106.0
GRID_COLS, GRID_ROWS = 10, 10
CELL_W, CELL_H = 7.0, 9.4
HEADER = 8.0                         # title band at the top of the poster
TEX = "textures\\Hearthcards\\"
RARITY = {"C": ("Common", (150, 112, 68)), "U": ("Uncommon", (110, 140, 170)), "R": ("Rare", (214, 168, 56))}


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def load_cards():
    cards = {}
    with open(os.path.join(ROOT, "tools", "cards.csv"), newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            n = int(row["number"])
            cards[n] = {"name": row["name"].strip(), "rarity": row["rarity"].strip().upper()[:1] or "C"}
    assert sorted(cards) == list(range(1, len(cards) + 1)), "cards.csv must number cards 1..N without gaps"
    assert len(cards) <= GRID_COLS * GRID_ROWS
    return cards


def find_art(stem):
    for ext in (".png", ".jpg", ".jpeg", ".webp"):
        p = os.path.join(ART, stem + ext)
        if os.path.exists(p):
            return p
    return None


def crop_to(img, aspect):
    w, h = img.size
    if w / h > aspect:
        nw = int(h * aspect)
        return img.crop(((w - nw) // 2, 0, (w - nw) // 2 + nw, h))
    nh = int(w / aspect)
    return img.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))


def parchment(w, h, seed, base=(222, 205, 168)):
    import random
    rnd = random.Random(seed)
    img = Image.new("RGB", (w, h), base)
    d = ImageDraw.Draw(img)
    for _ in range(w * h // 900):
        x, y = rnd.randrange(w), rnd.randrange(h)
        r = rnd.randrange(4, 26)
        k = rnd.randrange(-14, 10)
        c = tuple(max(0, min(255, v + k)) for v in base)
        d.ellipse((x - r, y - r, x + r, y + r), fill=c)
    img = img.filter(ImageFilter.GaussianBlur(6))
    # darker edges
    mask = Image.new("L", (w, h), 0)
    md = ImageDraw.Draw(mask)
    for i in range(40):
        md.rectangle((i, i, w - 1 - i, h - 1 - i), outline=int(110 * (1 - i / 40) ** 2))
    dark = Image.new("RGB", (w, h), (120, 90, 50))
    return Image.composite(dark, img, mask)


def centered(d, xy, text, fnt, fill):
    x, y = xy
    b = d.textbbox((0, 0), text, font=fnt)
    d.text((x - (b[2] - b[0]) / 2 - b[0], y - (b[3] - b[1]) / 2 - b[1]), text, font=fnt, fill=fill)


def placeholder_card(n, card):
    W, H = 500, 700
    label, col = RARITY.get(card["rarity"], RARITY["C"])
    img = parchment(W, H, n)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((8, 8, W - 9, H - 9), 26, outline=col, width=16)
    d.rounded_rectangle((30, 30, W - 31, H - 31), 16, outline=(70, 50, 30), width=3)
    # emblem
    cx, cy, r = W // 2, 300, 120
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=col, width=10)
    pts = [(cx + r * 0.7 * math.cos(a), cy + r * 0.7 * math.sin(a)) for a in [i * math.pi / 2 - math.pi / 2 for i in range(4)]]
    d.polygon(pts, fill=col)
    centered(d, (cx, 90), f"No. {n:03}", font("Cinzel-SemiBold.ttf", 56), (60, 40, 25))
    centered(d, (cx, 500), card["name"], font("IMFellEnglish-Regular.ttf", 54), (40, 28, 18))
    centered(d, (cx, 600), label, font("IMFellEnglish-Italic.ttf", 40), col)
    return img


def card_image(n, card):
    p = find_art(os.path.join("cards", f"{n:03}"))
    if p:
        return crop_to(Image.open(p).convert("RGB"), CARD_W / CARD_H)
    return placeholder_card(n, card)


def card_back():
    p = find_art("back")
    if p:
        return crop_to(Image.open(p).convert("RGB"), CARD_W / CARD_H)
    W, H = 500, 700
    img = Image.new("RGB", (W, H), (70, 38, 26))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((8, 8, W - 9, H - 9), 26, outline=(196, 160, 80), width=12)
    for i in range(0, W + H, 40):
        d.line((i, 0, i - H, H), fill=(82, 46, 32), width=10)
    d.rounded_rectangle((60, 260, W - 60, 440), 20, fill=(70, 38, 26), outline=(196, 160, 80), width=6)
    centered(d, (W // 2, 350), "Hearthcards", font("Cinzel-SemiBold.ttf", 46), (220, 186, 100))
    return img


def slot_center(i):
    """Card i (1-based) on the poster: (x, z) in poster space. The front faces +Y, so the viewer's left is +X."""
    r, c = (i - 1) // GRID_COLS, (i - 1) % GRID_COLS
    gx = GRID_COLS * CELL_W / 2
    x = gx - CELL_W / 2 - CELL_W * c
    top = POSTER_H / 2 - HEADER
    z = top - CELL_H / 2 - CELL_H * r
    return x, z


def poster_image(cards):
    W, H = 1200, int(1200 * POSTER_H / POSTER_W)
    p = find_art("poster")
    img = crop_to(Image.open(p).convert("RGB"), POSTER_W / POSTER_H).resize((W, H)) if p else parchment(W, H, 7)
    d = ImageDraw.Draw(img)
    d.rectangle((6, 6, W - 7, H - 7), outline=(92, 62, 34), width=12)
    sx, sz = W / POSTER_W, H / POSTER_H
    centered(d, (W / 2, HEADER / 2 * sz + 4), "Hearthcards Collection", font("Cinzel-SemiBold.ttf", int(HEADER * sz * 0.55)), (70, 44, 22))
    fnum = font("Cinzel-SemiBold.ttf", 30)
    for i in range(1, len(cards) + 1):
        x, z = slot_center(i)
        u, v = (POSTER_W / 2 - x) * sx, (POSTER_H / 2 - z) * sz
        hw, hh = CARD_W / 2 * sx, CARD_H / 2 * sz
        col = RARITY.get(cards[i]["rarity"], RARITY["C"])[1]
        d.rounded_rectangle((u - hw, v - hh, u + hw, v + hh), 8, outline=col, width=4)
        centered(d, (u, v), f"{i}", fnum, (110, 84, 52))
    return img


def plain_parchment():
    return parchment(256, 256, 3)


def to_pow2(img, size):
    return img.resize(size, Image.LANCZOS)


def build_textures(out, cards):
    t = os.path.join(out, "textures", "Hearthcards")
    os.makedirs(os.path.join(t, "Cards"), exist_ok=True)
    for n, card in cards.items():
        dds.save(to_pow2(card_image(n, card), (512, 1024)), os.path.join(t, "Cards", f"Card{n:03}.dds"))
    dds.save(to_pow2(card_back(), (512, 1024)), os.path.join(t, "CardBack.dds"))
    dds.save(to_pow2(poster_image(cards), (1024, 2048)), os.path.join(t, "Poster.dds"))
    dds.save(to_pow2(plain_parchment(), (256, 256)), os.path.join(t, "Parchment.dds"))
    dds.save(Image.new("RGBA", (16, 16), (128, 128, 255, 0)), os.path.join(t, "Flat_n.dds"), alpha=True)


# ------------------------------------------------------------------ meshes
FLAT_N = TEX + "Flat_n.dds"


def card_meshes(n, z=0.05):
    hw, hh = CARD_W / 2, CARD_H / 2
    front = nif.Mesh(f"Card{n:03}", TEX + f"Cards\\Card{n:03}.dds", FLAT_N)
    front.quad((-hw, -hh, z), (hw, -hh, z), (hw, hh, z), (-hw, hh, z))
    back = nif.Mesh(f"Card{n:03}Back", TEX + "CardBack.dds", FLAT_N)
    back.quad((hw, -hh, -z), (-hw, -hh, -z), (-hw, hh, -z), (hw, hh, -z))
    return [front, back]


def poster_board(name, texture):
    m = nif.Mesh(name, texture, FLAT_N)
    m.box(-POSTER_W / 2, POSTER_W / 2, -0.5, 0.5, -POSTER_H / 2, POSTER_H / 2)
    return m


def poster_meshes(cards):
    meshes = [poster_board("Board", TEX + "Poster.dds")]
    hw, hh, y = CARD_W / 2, CARD_H / 2, 0.56
    for i in range(1, len(cards) + 1):
        x, z = slot_center(i)
        m = nif.Mesh(f"Card{i:03}", TEX + f"Cards\\Card{i:03}.dds", FLAT_N, flags=0xF)  # hidden until pinned
        m.quad((x + hw, y, z - hh), (x - hw, y, z - hh), (x - hw, y, z + hh), (x + hw, y, z + hh))
        meshes.append(m)
    return meshes


def ghost_meshes(color):
    m = poster_board("Ghost", TEX + "Poster.dds")
    m.sf1 = (1 << 31) | (1 << 22) | (1 << 8)  # zbuffer test, own emit, receive shadows (no casting)
    m.sf2 = (1 << 5) | (1 << 4)               # vertex colours, double sided, no zbuffer write
    m.emissive, m.emissive_mult = color, 1.2
    m.alpha = 0.45
    m.alpha_flags = 0x00ED                    # blend: src alpha / inv src alpha
    return [m]


def cylinder(m, radius, half_len, segs=16):
    for s in range(segs):
        a0, a1 = 2 * math.pi * s / segs, 2 * math.pi * (s + 1) / segs
        y0, z0 = radius * math.cos(a0), radius * math.sin(a0)
        y1, z1 = radius * math.cos(a1), radius * math.sin(a1)
        u0, u1 = s / segs, (s + 1) / segs
        m.quad((-half_len, y0, z0), (half_len, y0, z0), (half_len, y1, z1), (-half_len, y1, z1),
               ((0, u0), (1, u0), (1, u1), (0, u1)))


def rolled_meshes():
    paper = nif.Mesh("Roll", TEX + "Parchment.dds", FLAT_N)
    cylinder(paper, 2.0, 14.0)
    tie = nif.Mesh("Tie", "textures\\clutter\\common\\leatherstrips01.dds", "textures\\clutter\\common\\leatherstrips01_n.dds")
    for cx in (-7.0, 7.0):
        segs = 16
        for s in range(segs):
            a0, a1 = 2 * math.pi * s / segs, 2 * math.pi * (s + 1) / segs
            r = 2.12
            y0, z0, y1, z1 = r * math.cos(a0), r * math.sin(a0), r * math.cos(a1), r * math.sin(a1)
            tie.quad((cx - 0.6, y0, z0), (cx + 0.6, y0, z0), (cx + 0.6, y1, z1), (cx - 0.6, y1, z1))
    return [paper, tie]


def build_meshes(out, cards):
    m = os.path.join(out, "meshes", "Hearthcards")
    os.makedirs(os.path.join(m, "Cards"), exist_ok=True)
    card_coll = dict(half=(CARD_W / 2, CARD_H / 2, 0.25), layer=nif.LAYER_CLUTTER, mass=0.2, material=nif.MAT_BOOK, dynamic=True)
    for n in cards:
        nif.build(os.path.join(m, "Cards", f"Card{n:03}.nif"), f"Card{n:03}", card_meshes(n), card_coll, bsx=0xC2,
                  inv=(1571, 0, 0, 1.0))
    poster_coll = dict(half=(POSTER_W / 2, 0.5, POSTER_H / 2), layer=nif.LAYER_STATIC, material=nif.MAT_WOOD)
    nif.build(os.path.join(m, "Poster.nif"), "HearthcardsPoster", poster_meshes(cards), poster_coll, bsx=0x82)
    nif.build(os.path.join(m, "PosterGhost.nif"), "HearthcardsGhost", ghost_meshes((0.25, 1.0, 0.35)), None, bsx=0x0)
    nif.build(os.path.join(m, "PosterGhostRed.nif"), "HearthcardsGhostRed", ghost_meshes((1.0, 0.2, 0.15)), None, bsx=0x0)
    roll_coll = dict(half=(14.5, 2.2, 2.2), layer=nif.LAYER_CLUTTER, mass=1.0, material=nif.MAT_BOOK, dynamic=True)
    nif.build(os.path.join(m, "PosterRolled.nif"), "HearthcardsPosterRolled", rolled_meshes(), roll_coll, bsx=0xC2,
              inv=(0, 0, 785, 1.0))


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "Data")
    cards = load_cards()
    build_textures(out, cards)
    build_meshes(out, cards)
    print(f"wrote {len(cards)} cards, poster, ghost and rolled poster to {out}")


if __name__ == "__main__":
    main()

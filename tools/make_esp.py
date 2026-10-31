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

"""Generates Hearthcards.esp (ESL-flagged, no scripts) and src/EspIds.h from tools/cards.csv.

Records:
  MISC  HC_Card001..   - the cards (the inventory shows the 3D card)
  BOOK  HC_PosterRolled - the rolled-up poster (reading it starts placement; the DLL closes the book)
  CONT  HC_Poster       - the hung poster; its inventory holds the pinned cards
  STAT  HC_PosterGhost / HC_PosterGhostRed - the placement preview (fits / doesn't fit)
  COBJ  HC_RecipePoster - tanning rack: 1 Leather + 2 Leather Strips
  LVLI  HC_CardsCommon/Uncommon/Rare - one card of that rarity (the DLL leaves out cards you own)
  LVLI  HC_CardsLoot / HC_CardsVendor / HC_CardsDrawer - 6:3:1 common:uncommon:rare; the DLL adds them to
        vanilla lists and sets their chance from Hearthcards.ini

Usage: python3 tools/make_esp.py
"""
import csv
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
M = 0x01000000  # one master (Skyrim.esm) -> our records are 01xxxxxx in the file

IDS = {
    "Poster": 0x800, "PosterRolled": 0x801, "Ghost": 0x802, "GhostRed": 0x803, "Recipe": 0x804,
    "ListCommon": 0x805, "ListUncommon": 0x806, "ListRare": 0x807, "ListLoot": 0x808, "ListVendor": 0x809, "ListDrawer": 0x80A,
}
CARD_BASE = 0x900  # card n = CARD_BASE + n - 1

# Skyrim.esm
CRAFTING_TANNING_RACK = 0x0007866A
LEATHER = 0x000DB5D2
LEATHER_STRIPS = 0x000800E4
HIGH_POLY_NOTE = 0x00097788      # STAT: the book menu shows a note
ITM_NOTE_UP = 0x000C7A54         # SNDR
VENDOR_ITEM_BOOK = 0x000937A2    # KYWD
VENDOR_ITEM_CLUTTER = 0x000914E9 # KYWD

HDR_LINES = """// Hearthcards - collect trading cards and pin them to a poster in Skyrim Special Edition.
// Copyright (C) 2026 evodube
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
// even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with this program (LICENSE.txt).
// If not, see <https://www.gnu.org/licenses/>.

""".splitlines()

RARITY_NAMES = {"C": "Common", "U": "Uncommon", "R": "Rare"}
COUNT = 0


def sub(tag, data):
    return tag.encode() + struct.pack("<H", len(data)) + data


def zstr(s):
    return s.encode("cp1252") + b"\0"


def u8(v): return struct.pack("<B", v)
def u16(v): return struct.pack("<H", v)
def u32(v): return struct.pack("<I", v)
def i32(v): return struct.pack("<i", v)
def f32(v): return struct.pack("<f", v)


def obnd(x0, y0, z0, x1, y1, z1):
    return sub("OBND", struct.pack("<6h", x0, y0, z0, x1, y1, z1))


def fid(name):
    return M | IDS[name]


def card_fid(n):
    return M | (CARD_BASE + n - 1)


def record(tag, formid, payload, flags=0):
    global COUNT
    COUNT += 1
    return tag.encode() + struct.pack("<IIIHHHH", len(payload), flags, formid, 0, 0, 44, 0) + payload


def group(label, payload):
    return b"GRUP" + struct.pack("<I4siHHI", 24 + len(payload), label.encode(), 0, 0, 0, 0) + payload


def load_cards():
    cards = {}
    with open(os.path.join(ROOT, "tools", "cards.csv"), newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            cards[int(row["number"])] = (row["name"].strip(), (row["rarity"].strip().upper()[:1] or "C"))
    assert sorted(cards) == list(range(1, len(cards) + 1))
    return cards


def card_title(n, name):
    return f"Trading Card No. {n:03}" + (f" - {name}" if name else "")


def book(formid, edid, full, model, desc, value, weight):
    return record("BOOK", formid,
                  sub("EDID", zstr(edid)) + obnd(-3, -4, 0, 3, 4, 0) + sub("FULL", zstr(full)) +
                  sub("MODL", zstr(model)) + sub("DESC", zstr(desc)) + sub("YNAM", u32(ITM_NOTE_UP)) +
                  sub("KSIZ", u32(1)) + sub("KWDA", u32(VENDOR_ITEM_BOOK)) +
                  # flags, type (0 book; the inventory art makes it a note), teaches (none), value, weight
                  sub("DATA", u8(0) + u8(0) + u16(0) + i32(-1) + u32(value) + f32(weight)) +
                  sub("INAM", u32(HIGH_POLY_NOTE)) + sub("CNAM", zstr("")))


def lvli(formid, edid, entries, chance_none=0, flags=0x01):
    body = sub("EDID", zstr(edid)) + obnd(0, 0, 0, 0, 0, 0) + sub("LVLD", u8(chance_none)) + sub("LVLF", u8(flags))
    body += sub("LLCT", u8(len(entries)))
    for form in entries:
        body += sub("LVLO", u16(1) + u16(0) + u32(form) + u16(1) + u16(0))
    return record("LVLI", formid, body)


def stat(formid, edid, model):
    return record("STAT", formid, sub("EDID", zstr(edid)) + obnd(-40, -1, -53, 40, 1, 53) + sub("MODL", zstr(model)) +
                  sub("DNAM", f32(90.0) + u32(0)))


def main():
    cards = load_cards()
    misc = b""
    for n, (name, rarity) in cards.items():
        # a plain item: the inventory shows the 3D card (rotate / zoom to see both sides)
        value = {"C": 5, "U": 15, "R": 50}[rarity]
        misc += record("MISC", card_fid(n),
                       sub("EDID", zstr(f"HC_Card{n:03}")) + obnd(-3, -4, 0, 3, 4, 0) + sub("FULL", zstr(card_title(n, name))) +
                       sub("MODL", zstr(f"Hearthcards\\Cards\\Card{n:03}.nif")) + sub("YNAM", u32(ITM_NOTE_UP)) +
                       sub("ZNAM", u32(ITM_NOTE_UP)) + sub("KSIZ", u32(1)) + sub("KWDA", u32(VENDOR_ITEM_CLUTTER)) +
                       sub("DATA", u32(value) + f32(0.0)))
    books = book(fid("PosterRolled"), "HC_PosterRolled", "Card Poster (rolled)", "Hearthcards\\PosterRolled.nif",
                  "A rolled-up poster for a trading card collection.", 25, 1.0)

    cont = record("CONT", fid("Poster"),
                  sub("EDID", zstr("HC_Poster")) + obnd(-40, -1, -53, 40, 1, 53) + sub("FULL", zstr("Card Poster")) +
                  sub("MODL", zstr("Hearthcards\\Poster.nif")) + sub("DATA", u8(0) + f32(0.0)))

    stats = stat(fid("Ghost"), "HC_PosterGhost", "Hearthcards\\PosterGhost.nif")
    stats += stat(fid("GhostRed"), "HC_PosterGhostRed", "Hearthcards\\PosterGhostRed.nif")

    cobj = record("COBJ", fid("Recipe"),
                  sub("EDID", zstr("HC_RecipePoster")) + sub("COCT", u32(2)) +
                  sub("CNTO", u32(LEATHER) + u32(1)) + sub("CNTO", u32(LEATHER_STRIPS) + u32(2)) +
                  sub("CNAM", u32(fid("PosterRolled"))) + sub("BNAM", u32(CRAFTING_TANNING_RACK)) + sub("NAM1", u16(1)))

    by = {r: [card_fid(n) for n, (_, rr) in cards.items() if rr == r] for r in "CUR"}
    lists = lvli(fid("ListCommon"), "HC_CardsCommon", by["C"])
    lists += lvli(fid("ListUncommon"), "HC_CardsUncommon", by["U"])
    lists += lvli(fid("ListRare"), "HC_CardsRare", by["R"])
    weighted = [fid("ListCommon")] * 6 + [fid("ListUncommon")] * 3 + [fid("ListRare")]
    lists += lvli(fid("ListLoot"), "HC_CardsLoot", weighted, chance_none=85)
    lists += lvli(fid("ListVendor"), "HC_CardsVendor", weighted)
    lists += lvli(fid("ListDrawer"), "HC_CardsDrawer", weighted)

    body = (group("BOOK", books) + group("MISC", misc) + group("CONT", cont) + group("STAT", stats) + group("COBJ", cobj) + group("LVLI", lists))
    groups = 6
    next_id = CARD_BASE + len(cards)
    hedr = struct.pack("<fiI", 1.70, COUNT + groups, next_id)
    tes4 = (sub("HEDR", hedr) + sub("CNAM", zstr("evodube")) +
            sub("SNAM", zstr("Hearthcards - trading cards and a poster to pin them on (needs Hearthcards.dll)")) +
            sub("MAST", zstr("Skyrim.esm")) + sub("DATA", b"\0" * 8))
    esp = record("TES4", 0, tes4, flags=0x200) + body  # 0x200 = ESL (light plugin)
    open(os.path.join(ROOT, "Hearthcards.esp"), "wb").write(esp)

    L = [*HDR_LINES, "#pragma once", "// Generated by tools/make_esp.py from tools/cards.csv - keep in sync with Hearthcards.esp.", "",
         "namespace EspIds", "{", '\tinline constexpr const char* kPlugin = "Hearthcards.esp";']
    for k, v in IDS.items():
        L.append(f"\tinline constexpr RE::FormID k{k} = 0x{v:X};")
    L.append(f"\tinline constexpr RE::FormID kCardBase = 0x{CARD_BASE:X};")
    L.append(f"\tinline constexpr int kCardCount = {len(cards)};")
    L.append("\t// rarity per card (index 0 = card 1): 0 common, 1 uncommon, 2 rare")
    L.append("\tinline constexpr std::uint8_t kRarity[kCardCount] = { " +
             ", ".join(str("CUR".index(r)) for _, (_, r) in sorted(cards.items())) + " };")
    L += ["}", ""]
    open(os.path.join(ROOT, "src", "EspIds.h"), "w").write("\n".join(L))
    print(f"wrote Hearthcards.esp ({len(esp)} bytes, {COUNT} records) and src/EspIds.h")


if __name__ == "__main__":
    main()

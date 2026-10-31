Card art for Hearthcards (used by tools/make_assets.py; the CI build picks it up automatically).

  art/cards/001.png ... art/cards/100.png   one image per card (png, jpg or webp, any size).
                                           Cropped to a 5:7 portrait card from the middle.
  art/back.png                             optional card back
  art/poster.png                           optional poster background (80:106 portrait); the numbered
                                           slots and the title are drawn on top

A card without an image gets a placeholder (its number, name and rarity frame).
Card names and rarities (C / U / R) live in tools/cards.csv. After changing cards.csv, run
  python3 tools/make_esp.py
and commit Hearthcards.esp and src/EspIds.h too.

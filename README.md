# Hearthcards

Collect 100 trading cards across Skyrim and pin them to a poster on your wall.

Skyrim Special Edition 1.5.97 and Anniversary Edition 1.6.x (Steam and GOG), one DLL. Needs
[SKSE64](https://skse.silverlock.org/) and [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444).

## In game

- **Finding cards.** Cards turn up on bandit, draugr, falmer, Forsworn, warlock, vampire and Thalmor
  corpses and chests, in citizens' pockets, and in drawers and end tables. Every general goods merchant
  (Belethor, Lucan, Sayma, Birna, the Khajiit caravans...) always has 3 different cards you don't own yet,
  a fresh set every 2 days. 60 common, 30 uncommon, 10 rare. In your inventory each card shows as a 3D card.
- **No duplicates.** A copy of a card you already have (in your inventory, on a poster, or rolled up with a
  poster) never stays in your inventory, and cards you own stop appearing in loot.
- **The poster.** Craft a *Card Poster (rolled)* at a tanning rack (1 Leather, 2 Leather Strips).
  Use it from your inventory and a see-through poster follows your crosshair onto any wall (like
  Hearthframe's frames). **Left-click** or **Activate** hangs it, the **mouse wheel** changes its size,
  **Up/Down** push it into / out from the wall, **right-click** or **Esc** cancels.
- **Using the poster.** Activate the hung poster for a menu:
  - **Pin Cards** opens the poster's inventory. Put cards in and each one appears in its own numbered spot;
    take one out and its spot is empty again. Only cards can go in.
  - **Move** picks it up into placement mode again.
  - **Take Down** rolls it back into your inventory; its cards stay with it and are pinned again when you hang it.

## Your own card art

Drop pictures into `Data\SKSE\Plugins\Hearthcards\Cards\` and start the game - they're converted to
the card textures automatically. Name them by card number: `037.png`, or `037 - The Dragonborn [R].png` to
also set the name and rarity (`[C]`, `[U]`, `[R]`); `back.png` is the card back. Any size, png/jpg/bmp/tga,
cropped to a 5:7 card. Details in `Data\SKSE\Plugins\Hearthcards\Cards\README.txt`.

## Settings

`Data\SKSE\Plugins\Hearthcards.ini`: loot chances, which loot gets cards, the duplicate rule, and how far
away a poster can be hung.

## Building

GitHub Actions builds everything on every push (`.github/workflows/build.yml`); the ready-to-install mod
lands on the `dist` branch and as the `Hearthcards` artifact.

- `tools/cards.csv` - card names and rarities. After changing it run `python3 tools/make_esp.py`
  (rewrites `Hearthcards.esp` and `src/EspIds.h`).
- `art/` - card art; see `art/README.txt`. Cards without art get a placeholder.
- `tools/make_assets.py` - builds every mesh and texture (needs Pillow 11+).
- `src/` - the SKSE plugin (CommonLibSSE-NG).

## License

GPL-3.0, see `LICENSE.txt`.

Hearthcards - your own card art
================================

Put pictures in this folder and start the game: Hearthcards turns them into the card textures
automatically (it only redoes a picture when you change it). No other tools needed.

File names start with the card number (1-100):

  37.png  or  037.png                  card 37's art
  037 - The Dragonborn.png             art + name  -> "Trading Card No. 037 - The Dragonborn"
  037 - The Dragonborn [R].png         art + name + rarity: [C] common, [U] uncommon, [R] rare
  back.png                             the back of every card

  png, jpg, bmp or tga, any size. Pictures are fitted onto a 5:7 portrait card whole - nothing is cut
  off; if a picture is a different shape, the leftover strip is filled with its own edge colour.
  500 x 700, 1000 x 1400 and so on fit exactly. (Hearthcards.ini [Art] bCropArt=1 crops instead.)

  Cards without a picture keep the built-in art, name and rarity (01-60 common, 61-90 uncommon, 91-100 rare).

With Mod Organizer 2: make a new empty mod (e.g. "My Hearthcards Art"), put the pictures in
  My Hearthcards Art\SKSE\Plugins\Hearthcards\Cards\
and enable it. The converted textures land in MO2's Overwrite folder; you can move them into your
mod afterwards (textures\Hearthcards\...) to share the set with others as a normal texture mod.

What happened is written to Documents\My Games\Skyrim Special Edition\SKSE\Hearthcards.log
(lines starting "Card art:").

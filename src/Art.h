// Hearthcards - collect trading cards and pin them to a poster in Skyrim Special Edition.
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

#pragma once

// Custom card art: pictures dropped into Data\SKSE\Plugins\Hearthcards\Cards are turned into the
// card textures when the game starts.
//
//   037.png                          -> card 37's art
//   037 - The Dragonborn.png         -> card 37's art and name ("Trading Card No. 037 - The Dragonborn")
//   037 - The Dragonborn [R].png     -> ... and rarity: [C] common, [U] uncommon, [R] rare
//   back.png                         -> the back of every card
//
// png, jpg and jpeg, any size (stb_image, falling back to the Windows decoder); stretched to a 5:7 portrait card (iArtFit: fit or crop instead). Transparent parts stay see-through (bTransparency).
namespace Art
{
	struct Card
	{
		std::filesystem::path file;  // empty: no picture for this card
		std::string           name;  // empty: keep the plugin's name
		int                   rarity = -1;  // -1: keep the plugin's rarity
	};

	// Plugin load: reads the folder and converts new or changed pictures on a background thread.
	void Start(int a_cardCount);

	// The pictures found for card number a_number (1-based).
	const Card* Get(int a_number);
}

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

// Data\SKSE\Plugins\Hearthcards.ini
namespace Config
{
	struct Values
	{
		int   lootChance = 10;     // % per bandit/draugr/falmer/... corpse or dungeon chest, and per citizen's pockets
		int   drawerChance = 100;  // % when a drawer/end table's misc roll lands on the card entry (about 1 roll in 11)
		int   vendorChance = 100;  // unused since 0.2.0 (merchants carry iMerchantCards instead)
		int   merchantCards = 3;   // different cards every general goods merchant carries, refreshed every 2 days
		bool  loot = true;
		bool  drawers = true;
		bool  vendors = true;
		bool  noDuplicates = true;  // a copy of a card you already have never stays in your inventory
		float reach = 350.0f;       // how far from you a poster can be hung (game units)
		int   flipKey = 0x30;      // DirectInput scan code; B: turn the selected card over in the inventory
		int   artFit = 0;          // card pictures: 0 stretch to the card, 1 fit whole (pad), 2 crop
		bool  transparency = true; // card pictures keep their transparent parts (see-through card outline)
		bool  logDebug = false;
	};

	void          Load();
	const Values& Get();
}

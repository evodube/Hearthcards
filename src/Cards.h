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

// The cards, the posters that hold them, loot, duplicates and the save data.
namespace Cards
{
	void Install();       // plugin load: serialization callbacks
	bool OnDataLoaded();  // forms, loot lists, hooks, event sinks; false = Hearthcards.esp missing
	void OnGameLoaded();  // new game / save loaded
	void Tick();          // main thread, once per frame

	RE::TESObjectCONT* PosterForm();
	RE::TESObjectBOOK* RolledForm();
	RE::TESObjectSTAT* GhostForm(bool a_red);

	// Placement hung a new poster: remember it and pin the cards it held when it was taken down.
	void OnPosterPlaced(RE::TESObjectREFR* a_poster);
	// Hide/show the poster's 3D (while it is being moved).
	void SetPosterHidden(RE::TESObjectREFR* a_poster, bool a_hidden);

	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where);
}

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

// Placement mode: a see-through poster follows the crosshair and snaps to walls.
// Activate hangs it, right-click / Esc (gamepad B) cancels.
namespace Placement
{
	void Install();                               // kDataLoaded: input listener
	void RequestNew();                            // the rolled poster was used: start once the menus are closed
	void RequestMove(RE::ObjectRefHandle a_poster);
	bool Active();
	void Update();                                // main thread, once per frame
	void Cancel();                                // loading screen: puts a moved poster back
	void Reset();                                 // save being loaded: forget everything, touch nothing in the world
	std::vector<RE::FormID> GhostIds();           // the preview refs (saved, so a save made mid-placement can clean them up)
}

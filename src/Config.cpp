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

#include "Config.h"

namespace Config
{
	namespace
	{
		Values      g_v;
		std::string g_path;

		int ReadInt(const char* a_sec, const char* a_key, int a_def)
		{
			return static_cast<int>(::GetPrivateProfileIntA(a_sec, a_key, a_def, g_path.c_str()));
		}

		float ReadFloat(const char* a_sec, const char* a_key, float a_def)
		{
			char buf[64]{};
			::GetPrivateProfileStringA(a_sec, a_key, "", buf, sizeof buf, g_path.c_str());
			if (!buf[0]) {
				return a_def;
			}
			char*       end = nullptr;
			const float f = std::strtof(buf, &end);
			return end != buf ? f : a_def;
		}
	}

	void Load()
	{
		std::error_code ec;
		g_path = std::filesystem::absolute("Data\\SKSE\\Plugins\\Hearthcards.ini", ec).string();
		Values d;
		g_v.lootChance = std::clamp(ReadInt("Loot", "iLootChance", d.lootChance), 0, 100);
		g_v.drawerChance = std::clamp(ReadInt("Loot", "iDrawerChance", d.drawerChance), 0, 100);
		g_v.vendorChance = std::clamp(ReadInt("Loot", "iMerchantChance", d.vendorChance), 0, 100);
		g_v.loot = ReadInt("Loot", "bLoot", 1) != 0;
		g_v.drawers = ReadInt("Loot", "bDrawers", 1) != 0;
		g_v.vendors = ReadInt("Loot", "bMerchants", 1) != 0;
		g_v.merchantCards = std::clamp(ReadInt("Loot", "iMerchantCards", d.merchantCards), 0, 10);
		g_v.noDuplicates = ReadInt("General", "bNoDuplicates", 1) != 0;
		g_v.reach = std::clamp(ReadFloat("Poster", "fReach", d.reach), 100.0f, 2000.0f);
		g_v.logDebug = ReadInt("General", "bDebugLog", 0) != 0;
		g_v.transparency = ReadInt("Art", "bTransparency", 1) != 0;
		g_v.artFit = std::clamp(ReadInt("Art", "iArtFit", d.artFit), 0, 2);
		g_v.flipKey = std::clamp(ReadInt("General", "iFlipKey", d.flipKey), 1, 255);
		spdlog::info("Config: loot {} {}%, drawers {} {}%, merchants {} {} cards, no duplicates {}, reach {:.0f}", g_v.loot, g_v.lootChance,
			g_v.drawers, g_v.drawerChance, g_v.vendors, g_v.merchantCards, g_v.noDuplicates, g_v.reach);
	}

	const Values& Get() { return g_v; }
}

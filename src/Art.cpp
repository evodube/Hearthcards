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

#include "Art.h"

#include "Config.h"
#include "Dds.h"

namespace fs = std::filesystem;

namespace Art
{
	namespace
	{
		constexpr float kCardAspect = 6.0f / 8.4f;  // the card mesh (5:7)
		constexpr int   kTexW = 512;                // stretched to 512 x 1024; the mesh's UVs undo the stretch
		constexpr int   kTexH = 1024;

		std::vector<Card> g_cards;  // index 0 = card 1
		Card              g_back;

		const fs::path kFolder = L"Data\\SKSE\\Plugins\\Hearthcards\\Cards";
		const fs::path kManifest = L"Data\\SKSE\\Plugins\\Hearthcards\\Converted.txt";

		std::string ToUtf8(const fs::path& a_p)
		{
			const auto s = a_p.u8string();
			return std::string(s.begin(), s.end());
		}

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::string Trim(const std::string& a_s, const char* a_chars = " \t-_.")
		{
			const auto b = a_s.find_first_not_of(a_chars);
			if (b == std::string::npos) {
				return {};
			}
			const auto e = a_s.find_last_not_of(a_chars);
			return a_s.substr(b, e - b + 1);
		}

		bool IsPicture(const fs::path& a_p)
		{
			const auto ext = Lower(ToUtf8(a_p.extension()));
			return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
		}

		// "037 - The Dragonborn [R]" -> 37, "The Dragonborn", 2
		bool ParseName(const std::string& a_stem, int& a_number, std::string& a_name, int& a_rarity)
		{
			std::size_t i = 0;
			while (i < a_stem.size() && std::isdigit(static_cast<unsigned char>(a_stem[i]))) {
				++i;
			}
			if (i == 0 || i > 4) {
				return false;
			}
			a_number = std::stoi(a_stem.substr(0, i));
			std::string rest = a_stem.substr(i);
			a_rarity = -1;
			// rarity tag: [C] [U] [R] or (Common) (Uncommon) (Rare), any case
			static const std::array<std::pair<const char*, int>, 9> kTags{ { { "[c]", 0 }, { "[u]", 1 }, { "[r]", 2 },
				{ "(common)", 0 }, { "(uncommon)", 1 }, { "(rare)", 2 }, { "[common]", 0 }, { "[uncommon]", 1 }, { "[rare]", 2 } } };
			const auto low = Lower(rest);
			for (const auto& [tag, r] : kTags) {
				if (const auto at = low.find(tag); at != std::string::npos) {
					a_rarity = r;
					rest.erase(at, std::strlen(tag));
					break;
				}
			}
			a_name = Trim(rest);
			return true;
		}

		struct Stamp
		{
			std::uintmax_t size = 0;
			long long      time = 0;
			bool operator==(const Stamp&) const = default;
		};

		Stamp StampOf(const fs::path& a_p)
		{
			std::error_code ec;
			Stamp           s;
			s.size = fs::file_size(a_p, ec);
			s.time = static_cast<long long>(fs::last_write_time(a_p, ec).time_since_epoch().count());
			return s;
		}

		// target dds -> stamp of the picture it was made from
		std::map<std::string, std::pair<std::string, Stamp>> ReadManifest()
		{
			std::map<std::string, std::pair<std::string, Stamp>> m;
			std::ifstream                                       f(kManifest);
			std::string                                         line;
			while (std::getline(f, line)) {
				std::istringstream ss(line);
				std::string        target, source, size, time;
				if (std::getline(ss, target, '|') && std::getline(ss, source, '|') && std::getline(ss, size, '|') && std::getline(ss, time)) {
					try {
						m[target] = { source, { std::stoull(size), std::stoll(time) } };
					} catch (...) {
					}
				}
			}
			return m;
		}

		void WriteManifest(const std::map<std::string, std::pair<std::string, Stamp>>& a_m)
		{
			std::error_code ec;
			fs::create_directories(kManifest.parent_path(), ec);
			std::ofstream f(kManifest, std::ios::trunc);
			for (const auto& [target, v] : a_m) {
				f << target << '|' << v.first << '|' << v.second.size << '|' << v.second.time << '\n';
			}
		}

		struct Job
		{
			fs::path    source;
			fs::path    target;
			std::string key;
		};

		void Convert(std::vector<Job> a_jobs)
		{
			auto manifest = ReadManifest();
			int  made = 0, skipped = 0, failed = 0;
			for (const auto& j : a_jobs) {
				const auto stamp = StampOf(j.source);
				const auto it = manifest.find(j.key);
				std::error_code ec;
				// the fit mode and the converter version are part of the record, so changing iArtFit (or updating
				// from an older version) redoes pictures; #o: opaque converter (0.8.5 - 0.8.3/0.8.4 kept transparency)
				static constexpr std::array<const char*, 3> kModeTags{ "#stretch", "#fit", "#crop" };
				const int  mode = Config::Get().artFit;
				const auto tag = ToUtf8(j.source.filename()) + kModeTags[mode] + "#o";
				if (it != manifest.end() && it->second.first == tag && it->second.second == stamp && fs::exists(j.target, ec)) {
					++skipped;
					continue;
				}
				std::string err;
				const auto  t0 = std::chrono::steady_clock::now();
				if (Dds::ConvertPicture(j.source, kCardAspect, kTexW, kTexH, j.target, err, static_cast<Dds::FitMode>(mode))) {
					manifest[j.key] = { tag, stamp };
					++made;
					spdlog::info("Card art: {} -> {} ({} ms)", ToUtf8(j.source.filename()), ToUtf8(j.target),
						std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
				} else {
					++failed;
					spdlog::error("Card art: {} could not be converted: {}", ToUtf8(j.source.filename()), err);
				}
			}
			if (made) {
				WriteManifest(manifest);
			}
			spdlog::info("Card art: {} converted, {} already up to date, {} failed", made, skipped, failed);
		}
	}

	void Start(int a_cardCount)
	{
		g_cards.assign(a_cardCount, {});
		std::error_code ec;
		fs::create_directories(kFolder, ec);
		std::vector<Job> jobs;
		int              found = 0;
		for (fs::directory_iterator it(kFolder, ec), end; !ec && it != end; it.increment(ec)) {
			if (!it->is_regular_file(ec) || !IsPicture(it->path())) {
				continue;
			}
			const auto  stem = ToUtf8(it->path().stem());
			if (Lower(stem) == "back") {
				g_back.file = it->path();
				jobs.push_back({ it->path(), fs::path(L"Data\\textures\\Hearthcards\\CardBack.dds"), "back" });
				continue;
			}
			int         number = 0, rarity = -1;
			std::string name;
			if (!ParseName(stem, number, name, rarity) || number < 1 || number > a_cardCount) {
				spdlog::warn("Card art: {} skipped - the file name must start with a card number 1-{}", ToUtf8(it->path().filename()), a_cardCount);
				continue;
			}
			auto& c = g_cards[number - 1];
			if (!c.file.empty()) {
				spdlog::warn("Card art: two pictures for card {} ({} and {}) - using the first", number, ToUtf8(c.file.filename()),
					ToUtf8(it->path().filename()));
				continue;
			}
			c.file = it->path();
			c.name = name;
			c.rarity = rarity;
			++found;
			jobs.push_back({ it->path(), fs::path(L"Data\\textures\\Hearthcards\\Cards") / fmt::format("Card{:03}.dds", number),
				fmt::format("{:03}", number) });
		}
		spdlog::info("Card art: {} card pictures{} in {}", found, g_back.file.empty() ? "" : " and a card back", ToUtf8(fs::absolute(kFolder, ec)));
		if (!jobs.empty()) {
			std::thread([jobs = std::move(jobs)]() mutable {
				::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
				try {
					Convert(std::move(jobs));
				} catch (const std::exception& e) {
					spdlog::error("Card art: conversion stopped: {}", e.what());
				}
			}).detach();
		}
	}

	const Card* Get(int a_number)
	{
		return a_number >= 1 && a_number <= static_cast<int>(g_cards.size()) ? &g_cards[a_number - 1] : nullptr;
	}
}

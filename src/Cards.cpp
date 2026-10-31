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

#include "Cards.h"

#include "Config.h"
#include "EspIds.h"
#include "Placement.h"

namespace Cards
{
	namespace
	{
		constexpr int kCards = EspIds::kCardCount;
		static_assert(kCards >= 1 && kCards <= 100, "the poster has 100 slots");
		using CardSet = std::bitset<100>;

		constexpr RE::FormID kPlayerId = 0x14;

		// Skyrim.esm leveled lists the cards are added to
		// "use all" lists: every entry rolls on its own -> a card with iLootChance %
		constexpr std::array<RE::FormID, 10> kLootLists{
			0x0010E0DD,  // LootBanditRandom
			0x0010FAC4,  // LootDraugrRandom
			0x0010F9D1,  // LootFalmerRandom
			0x0010FAB6,  // LootForswornRandom
			0x0010FACD,  // LootWarlockRandom
			0x0010FAD4,  // LootVampireRandom
			0x0010FAD8,  // LootThalmorRandom
			0x001046D1,  // LootCitizenPocketsCommon
			0x001046D3,  // LootCitizenPocketsRich
			0x001046D2,  // LootCitizenPocketsPoor
		};
		constexpr RE::FormID kDrawerList = 0x0010E8AA;  // LItemFurnitureMiscItems75 (drawers, end tables)
		constexpr RE::FormID kVendorList = 0x0009AF0A;  // LItemMiscVendorMiscItems75: its chests are the general goods merchants

		// ------------------------------------------------------------------ forms
		RE::TESObjectCONT*                    g_poster = nullptr;
		RE::TESObjectBOOK*                    g_rolled = nullptr;
		RE::TESObjectSTAT*                    g_ghost = nullptr;
		RE::TESObjectSTAT*                    g_ghostRed = nullptr;
		std::array<RE::TESLevItem*, 3>        g_tiers{};
		RE::TESLevItem*                       g_lootList = nullptr;
		RE::TESLevItem*                       g_vendorList = nullptr;
		RE::TESLevItem*                       g_drawerList = nullptr;
		std::array<RE::TESObjectBOOK*, kCards> g_cards{};
		std::unordered_map<RE::FormID, int>   g_cardIndex;
		std::array<RE::BSFixedString, kCards> g_slotNames;
		bool                                  g_ready = false;

		// ------------------------------------------------------------------ save data (main thread)
		std::vector<RE::FormID> g_posters;  // every hung poster
		std::vector<CardSet>    g_stash;    // cards of taken-down posters, re-pinned when a poster is hung again
		std::vector<RE::FormID> g_staleGhosts;
		std::unordered_map<RE::FormID, std::uint32_t> g_stocked;  // merchant chest -> restock period (2 days) last stocked

		std::unordered_set<RE::FormID> g_merchantChests;  // chest base forms of general goods merchants

		struct PosterView
		{
			RE::NiPointer<RE::NiAVObject>                root;
			std::array<RE::NiPointer<RE::NiAVObject>, kCards> slots;
			CardSet                                      pinned;
			bool                                         known = false;
			bool                                         hidden = false;
		};
		std::unordered_map<RE::FormID, PosterView> g_views;

		// ------------------------------------------------------------------ events -> main thread
		struct Return
		{
			RE::FormID poster;
			RE::FormID item;
			int        count;
		};
		std::mutex          g_evMtx;
		std::vector<int>    g_newCards;  // card indices the player just received
		std::vector<Return> g_returns;   // non-card items put into a poster
		std::atomic<bool>   g_dirty{ true };

		int CardIndex(RE::FormID a_id)
		{
			const auto it = g_cardIndex.find(a_id);
			return it == g_cardIndex.end() ? -1 : it->second;
		}

		bool IsPoster(const RE::TESObjectREFR* a_ref) { return a_ref && g_poster && a_ref->GetBaseObject() == g_poster; }

		std::string CardName(int a_i)
		{
			const char* n = g_cards[a_i] ? g_cards[a_i]->GetName() : nullptr;
			return n && *n ? n : fmt::format("Trading Card No. {:03}", a_i + 1);
		}

		void Notify(const std::string& a_text) { RE::DebugNotification(a_text.c_str()); }

		// ------------------------------------------------------------------ inventories
		std::array<int, kCards> CardCounts(RE::TESObjectREFR* a_ref)
		{
			std::array<int, kCards> out{};
			if (!a_ref) {
				return out;
			}
			const auto counts = a_ref->GetInventoryCounts([](RE::TESBoundObject& a_obj) {
				return a_obj.GetFormType() == RE::FormType::Book && g_cardIndex.contains(a_obj.GetFormID());
			});
			for (const auto& [obj, n] : counts) {
				const int i = obj ? CardIndex(obj->GetFormID()) : -1;
				if (i >= 0 && n > 0) {
					out[i] += n;
				}
			}
			return out;
		}

		CardSet CardsIn(RE::TESObjectREFR* a_ref)
		{
			CardSet s;
			const auto c = CardCounts(a_ref);
			for (int i = 0; i < kCards; ++i) {
				if (c[i] > 0) {
					s.set(i);
				}
			}
			return s;
		}

		RE::TESObjectREFR* PosterRef(RE::FormID a_id)
		{
			auto* ref = RE::TESForm::LookupByID<RE::TESObjectREFR>(a_id);
			return ref && !ref->IsDeleted() && IsPoster(ref) ? ref : nullptr;
		}

		CardSet PinnedEverywhere()
		{
			CardSet s;
			for (const auto id : g_posters) {
				s |= CardsIn(PosterRef(id));
			}
			for (const auto& st : g_stash) {
				s |= st;
			}
			return s;
		}

		// ------------------------------------------------------------------ leveled lists
		void SetEntries(RE::TESLevItem* a_list, const std::vector<RE::TESForm*>& a_forms)
		{
			if (!a_list) {
				return;
			}
			const auto n = (std::min)(a_forms.size(), std::size_t{ 255 });
			a_list->entries.resize(n);
			for (std::size_t i = 0; i < n; ++i) {
				auto& e = a_list->entries[i];
				e.form = a_forms[i];
				e.count = 1;
				e.level = 1;
				e.pad0C = 0;
				e.itemExtra = nullptr;
			}
			a_list->numEntries = static_cast<std::uint8_t>(n);
		}

		bool Inject(RE::FormID a_target, RE::TESLevItem* a_wrapper)
		{
			auto* list = RE::TESForm::LookupByID<RE::TESLevItem>(a_target);
			if (!list || !a_wrapper) {
				spdlog::warn("Leveled list {:08X} not found", a_target);
				return false;
			}
			for (const auto& e : list->entries) {
				if (e.form == a_wrapper) {
					return true;
				}
			}
			const auto n = list->entries.size();
			if (n >= 255) {
				spdlog::warn("Leveled list {:08X} is full", a_target);
				return false;
			}
			list->entries.resize(n + 1);
			for (auto i = n; i > 0; --i) {  // entries are sorted by level: ours (level 1) goes first
				list->entries[i] = list->entries[i - 1];
			}
			auto& e = list->entries[0];
			e.form = a_wrapper;
			e.count = 1;
			e.level = 1;
			e.pad0C = 0;
			e.itemExtra = nullptr;
			list->numEntries = static_cast<std::uint8_t>(n + 1);
			return true;
		}

		// The rarity lists hold only cards you don't have yet; the weighted lists skip empty rarities.
		void RebuildLists(const CardSet& a_owned)
		{
			std::array<std::vector<RE::TESForm*>, 3> by;
			for (int i = 0; i < kCards; ++i) {
				if (g_cards[i] && (!Config::Get().noDuplicates || !a_owned.test(i))) {
					by[EspIds::kRarity[i]].push_back(g_cards[i]);
				}
			}
			std::vector<RE::TESForm*> weighted;
			constexpr std::array<int, 3> kWeight{ 6, 3, 1 };
			for (int t = 0; t < 3; ++t) {
				SetEntries(g_tiers[t], by[t]);
				if (!by[t].empty() && g_tiers[t]) {
					weighted.insert(weighted.end(), kWeight[t], g_tiers[t]);
				}
			}
			for (auto* l : { g_lootList, g_vendorList, g_drawerList }) {
				SetEntries(l, weighted);
			}
			if (Config::Get().logDebug) {
				spdlog::info("Lists rebuilt: {} / {} / {} cards left (common / uncommon / rare)", by[0].size(), by[1].size(), by[2].size());
			}
		}

		// ------------------------------------------------------------------ poster 3D
		void RefreshViews(bool a_reread)
		{
			for (auto it = g_posters.begin(); it != g_posters.end();) {
				auto* ref = PosterRef(*it);
				if (!ref) {
					g_views.erase(*it);
					it = g_posters.erase(it);
					continue;
				}
				auto& v = g_views[*it];
				if (a_reread || !v.known) {
					v.pinned = CardsIn(ref);
					v.known = true;
				}
				auto* root = ref->Get3D();
				if (!root) {
					v.root.reset();
					for (auto& s : v.slots) {
						s.reset();
					}
					++it;
					continue;
				}
				if (root != v.root.get()) {
					v.root.reset(root);
					for (int i = 0; i < kCards; ++i) {
						v.slots[i].reset(root->GetObjectByName(g_slotNames[i]));
					}
				}
				if (root->GetAppCulled() != v.hidden) {
					root->SetAppCulled(v.hidden);
				}
				for (int i = 0; i < kCards; ++i) {
					if (auto* s = v.slots[i].get()) {
						const bool hide = !v.pinned.test(i);
						if (s->GetAppCulled() != hide) {
							s->SetAppCulled(hide);
						}
					}
				}
				++it;
			}
		}

		// ------------------------------------------------------------------ duplicates
		void ProcessNewCards(std::vector<int>& a_cards)
		{
			if (!Config::Get().noDuplicates || a_cards.empty()) {
				return;
			}
			std::sort(a_cards.begin(), a_cards.end());
			a_cards.erase(std::unique(a_cards.begin(), a_cards.end()), a_cards.end());
			auto*         player = RE::PlayerCharacter::GetSingleton();
			const CardSet elsewhere = PinnedEverywhere();
			const auto    have = CardCounts(player);
			for (const int i : a_cards) {
				const int keep = elsewhere.test(i) ? 0 : 1;
				if (have[i] > keep && g_cards[i]) {
					player->RemoveItem(g_cards[i], have[i] - keep, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
					Notify(fmt::format("You already have {}.", CardName(i)));
					spdlog::info("Duplicate {} removed ({} in inventory, {} elsewhere)", i + 1, have[i], elsewhere.test(i));
				}
			}
		}

		void ProcessReturns(std::vector<Return>& a_returns)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			bool  told = false;
			for (const auto& r : a_returns) {
				auto* poster = PosterRef(r.poster);
				auto* item = RE::TESForm::LookupByID<RE::TESBoundObject>(r.item);
				if (!poster || !item || r.count <= 0) {
					continue;
				}
				poster->RemoveItem(item, r.count, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, player);
				if (!told) {
					Notify("Only trading cards can be pinned to the poster.");
					told = true;
				}
			}
		}

		// ------------------------------------------------------------------ merchants
		CardSet OwnedNow()
		{
			CardSet    owned = PinnedEverywhere();
			const auto have = CardCounts(RE::PlayerCharacter::GetSingleton());
			for (int i = 0; i < kCards; ++i) {
				if (have[i] > 0) {
					owned.set(i);
				}
			}
			return owned;
		}

		// General goods merchants always have cards on the shelf: with every restock (2 days) a fresh set
		// of iMerchantCards different cards you don't own yet (6:3:1 common:uncommon:rare).
		void StockMerchant(RE::Actor* a_merchant)
		{
			const auto& cfg = Config::Get();
			if (!a_merchant || !cfg.vendors || cfg.merchantCards <= 0) {
				return;
			}
			auto* faction = a_merchant->GetVendorFaction();
			auto* chest = faction ? faction->vendorData.merchantContainer : nullptr;
			auto* base = chest ? chest->GetBaseObject() : nullptr;
			if (!base || !g_merchantChests.contains(base->GetFormID())) {
				return;
			}
			// Do the game's own restock first (it would otherwise run when Barter opens and wipe our cards):
			// same rule as the game, every iDaysToRespawnVendor days.
			auto*       cal = RE::Calendar::GetSingleton();
			const auto  today = static_cast<std::uint32_t>(cal ? cal->GetDaysPassed() : 0.0f);
			auto&       vendor = faction->vendorData;
			std::uint32_t respawnDays = 2;
			if (auto* gs = RE::GameSettingCollection::GetSingleton()) {
				if (auto* st = gs->GetSetting("iDaysToRespawnVendor")) {
					respawnDays = static_cast<std::uint32_t>((std::max)(1, st->GetSInt()));
				}
			}
			if (today >= vendor.lastDayReset + respawnDays || vendor.lastDayReset > today) {
				chest->ResetInventory(false);
				vendor.lastDayReset = today;
				spdlog::info("Merchant {:08X}: stock reset (day {})", a_merchant->GetFormID(), today);
			}
			const auto    period = vendor.lastDayReset + 1;  // one card set per vanilla restock
			const CardSet owned = OwnedNow();
			const auto    inChest = CardCounts(chest);
			const bool    restock = g_stocked[chest->GetFormID()] != period;
			for (int i = 0; i < kCards; ++i) {
				if (inChest[i] <= 0 || !g_cards[i]) {
					continue;
				}
				// a new period clears the shelf; cards you've got since then go; never two of one card
				const bool all = restock || (cfg.noDuplicates && owned.test(i));
				const int  drop = all ? inChest[i] : inChest[i] - 1;
				if (drop > 0) {
					chest->RemoveItem(g_cards[i], drop, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
			if (!restock) {
				return;  // what you bought this period stays bought
			}
			g_stocked[chest->GetFormID()] = period;
			std::vector<std::pair<int, int>> pool;  // card, weight
			constexpr std::array<int, 3>     kWeight{ 6, 3, 1 };
			for (int i = 0; i < kCards; ++i) {
				if (g_cards[i] && !(cfg.noDuplicates && owned.test(i))) {
					pool.emplace_back(i, kWeight[EspIds::kRarity[i]]);
				}
			}
			static std::mt19937 rng{ std::random_device{}() };
			std::string         added;
			for (int k = 0; k < cfg.merchantCards && !pool.empty(); ++k) {
				int total = 0;
				for (const auto& [i, w] : pool) {
					total += w;
				}
				int roll = std::uniform_int_distribution<int>(0, total - 1)(rng);
				auto it = pool.begin();
				for (; it != pool.end(); ++it) {
					if ((roll -= it->second) < 0) {
						break;
					}
				}
				if (it == pool.end()) {
					it = std::prev(pool.end());
				}
				chest->AddObjectToContainer(g_cards[it->first], nullptr, 1, nullptr);
				added += fmt::format("{}{}", added.empty() ? "" : ", ", it->first + 1);
				pool.erase(it);
			}
			spdlog::info("Merchant {:08X} restocked with cards {}", a_merchant->GetFormID(), added);
		}

		// ------------------------------------------------------------------ poster menu
		class MenuCallback final : public RE::IMessageBoxCallback
		{
		public:
			explicit MenuCallback(std::function<void(int)> a_fn) :
				_fn(std::move(a_fn)) {}
			~MenuCallback() override = default;

			void Run(Message a_msg) override
			{
				const int button = static_cast<int>(a_msg) - 4;
				if (_fn) {
					auto fn = _fn;
					SKSE::GetTaskInterface()->AddTask([fn, button] { fn(button); });
				}
			}

		private:
			std::function<void(int)> _fn;
		};

		bool ShowMessageBox(const std::string& a_body, const std::vector<std::string>& a_buttons, std::function<void(int)> a_fn)
		{
			auto* factory = RE::MessageDataFactoryManager::GetSingleton();
			auto* strings = RE::InterfaceStrings::GetSingleton();
			if (!factory || !strings) {
				return false;
			}
			const auto* creator = factory->GetCreator<RE::MessageBoxData>(strings->messageBoxData);
			auto*       box = creator ? creator->Create() : nullptr;
			if (!box) {
				return false;
			}
			box->callback = RE::make_smart<MenuCallback>(std::move(a_fn));
			box->bodyText = a_body.c_str();
			for (const auto& b : a_buttons) {
				box->buttonText.push_back(RE::BSString(b.c_str()));
			}
			box->QueueMessage();
			return true;
		}

		bool g_passThrough = false;

		void OpenPosterInventory(RE::ObjectRefHandle a_poster)
		{
			auto ref = a_poster.get();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ref || !player) {
				return;
			}
			g_passThrough = true;
			ref->ActivateRef(player, 0, nullptr, 0, false);
			g_passThrough = false;
		}

		void TakeDown(RE::ObjectRefHandle a_poster)
		{
			auto  ref = a_poster.get();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!ref || !player || !IsPoster(ref.get())) {
				return;
			}
			const auto counts = CardCounts(ref.get());
			CardSet    held;
			for (int i = 0; i < kCards; ++i) {
				if (counts[i] > 0 && g_cards[i]) {
					held.set(i);
					ref->RemoveItem(g_cards[i], counts[i], RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				}
			}
			// anything else that ended up inside goes back to the player
			const auto rest = ref->GetInventoryCounts();
			for (const auto& [obj, n] : rest) {
				if (obj && n > 0) {
					ref->RemoveItem(obj, n, RE::ITEM_REMOVE_REASON::kStoreInContainer, nullptr, player);
				}
			}
			if (held.any()) {
				g_stash.push_back(held);
			}
			const auto id = ref->GetFormID();
			std::erase(g_posters, id);
			g_views.erase(id);
			ref->Disable();
			ref->SetDelete(true);
			player->AddObjectToContainer(g_rolled, nullptr, 1, nullptr);
			Notify(held.any() ? fmt::format("You roll up the poster. Its {} cards stay pinned to it.", held.count()) :
			                    "You roll up the poster."s);
			g_dirty = true;
		}

		void ShowPosterMenu(RE::ObjectRefHandle a_poster)
		{
			auto ref = a_poster.get();
			if (!ref) {
				return;
			}
			const auto pinned = CardsIn(ref.get()).count();
			const auto body = fmt::format("Card Poster\n{} of {} cards pinned", pinned, kCards);
			ShowMessageBox(body, { "Pin Cards", "Move", "Take Down", "Cancel" }, [a_poster](int a_button) {
				switch (a_button) {
				case 0:
					OpenPosterInventory(a_poster);
					break;
				case 1:
					Placement::RequestMove(a_poster);
					break;
				case 2:
					TakeDown(a_poster);
					break;
				default:
					break;
				}
			});
		}

		// ------------------------------------------------------------------ activation hook (TESObjectCONT::Activate)
		using ActivateFn = bool (*)(RE::TESObjectCONT*, RE::TESObjectREFR*, RE::TESObjectREFR*, std::uint8_t, RE::TESBoundObject*, std::int32_t);
		ActivateFn g_origActivate = nullptr;

		bool ActivateHook(RE::TESObjectCONT* a_self, RE::TESObjectREFR* a_target, RE::TESObjectREFR* a_activator, std::uint8_t a_arg3,
			RE::TESBoundObject* a_object, std::int32_t a_count)
		{
			if (g_ready && a_self == g_poster && !g_passThrough && a_target && a_activator == RE::PlayerCharacter::GetSingleton()) {
				if (!Placement::Active()) {
					ShowPosterMenu(a_target->GetHandle());
				}
				return true;
			}
			return g_origActivate(a_self, a_target, a_activator, a_arg3, a_object, a_count);
		}

		// ------------------------------------------------------------------ event sinks
		class ContainerSink final : public RE::BSTEventSink<RE::TESContainerChangedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::TESContainerChangedEvent* a_event, RE::BSTEventSource<RE::TESContainerChangedEvent>*) override
			{
				if (!a_event || !g_ready) {
					return RE::BSEventNotifyControl::kContinue;
				}
				const int  card = CardIndex(a_event->baseObj);
				const auto toPoster = a_event->newContainer ? IsPoster(RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->newContainer)) : false;
				const auto fromPoster = a_event->oldContainer ? IsPoster(RE::TESForm::LookupByID<RE::TESObjectREFR>(a_event->oldContainer)) : false;
				if (card >= 0 || toPoster || fromPoster) {
					g_dirty = true;
				}
				std::lock_guard lk(g_evMtx);
				if (card >= 0 && a_event->newContainer == kPlayerId) {
					g_newCards.push_back(card);
				}
				if (toPoster && card < 0) {
					g_returns.push_back({ a_event->newContainer, a_event->baseObj, a_event->itemCount });
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		ContainerSink g_containerSink;

		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || !g_ready || !a_event->opening) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (a_event->menuName == RE::BookMenu::MENU_NAME && RE::BookMenu::GetTargetForm() == g_rolled) {
					// The rolled poster was "read": close the book (and the inventory behind it) and start placing.
					const bool inWorld = RE::BookMenu::GetTargetReference() != nullptr;
					if (auto* q = RE::UIMessageQueue::GetSingleton()) {
						q->AddMessage(RE::BookMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
						if (!inWorld) {
							q->AddMessage(RE::InventoryMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
							q->AddMessage(RE::FavoritesMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
							q->AddMessage(RE::TweenMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
						}
					}
					if (inWorld) {
						Notify("Pick up the rolled poster first, then use it from your inventory.");
					} else {
						Placement::RequestNew();
					}
				} else if (a_event->menuName == RE::DialogueMenu::MENU_NAME) {
					SKSE::GetTaskInterface()->AddTask([] {
						auto* topics = RE::MenuTopicManager::GetSingleton();
						auto  speaker = topics ? topics->speaker.get() : RE::NiPointer<RE::TESObjectREFR>{};
						if (auto* actor = speaker ? speaker->As<RE::Actor>() : nullptr) {
							StockMerchant(actor);
						}
					});
				} else if (a_event->menuName == RE::LoadingMenu::MENU_NAME) {
					SKSE::GetTaskInterface()->AddTask([] { Placement::Cancel(); });
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		MenuSink g_menuSink;

		// ------------------------------------------------------------------ co-save
		constexpr std::uint32_t kSaveId = 'HCRD';
		constexpr std::uint32_t kRecPosters = 'POST';
		constexpr std::uint32_t kRecStash = 'STSH';
		constexpr std::uint32_t kRecGhosts = 'GHST';
		constexpr std::uint32_t kRecMerchants = 'MRCH';
		constexpr std::uint32_t kVersion = 1;

		void WriteIds(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, const std::vector<RE::FormID>& a_ids)
		{
			if (!a_intfc->OpenRecord(a_type, kVersion)) {
				return;
			}
			const auto n = static_cast<std::uint32_t>(a_ids.size());
			a_intfc->WriteRecordData(n);
			for (const auto id : a_ids) {
				a_intfc->WriteRecordData(id);
			}
		}

		std::vector<RE::FormID> ReadIds(SKSE::SerializationInterface* a_intfc)
		{
			std::vector<RE::FormID> out;
			std::uint32_t           n = 0;
			if (!a_intfc->ReadRecordData(n)) {
				return out;
			}
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::FormID id = 0, resolved = 0;
				if (!a_intfc->ReadRecordData(id)) {
					break;
				}
				if (a_intfc->ResolveFormID(id, resolved)) {
					out.push_back(resolved);
				}
			}
			return out;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			WriteIds(a_intfc, kRecPosters, g_posters);
			WriteIds(a_intfc, kRecGhosts, Placement::GhostIds());
			if (a_intfc->OpenRecord(kRecMerchants, kVersion)) {
				const auto n = static_cast<std::uint32_t>(g_stocked.size());
				a_intfc->WriteRecordData(n);
				for (const auto& [id, period] : g_stocked) {
					a_intfc->WriteRecordData(id);
					a_intfc->WriteRecordData(period);
				}
			}
			if (a_intfc->OpenRecord(kRecStash, kVersion)) {
				const auto n = static_cast<std::uint32_t>(g_stash.size());
				a_intfc->WriteRecordData(n);
				for (const auto& s : g_stash) {
					std::uint64_t w[2]{};
					for (int i = 0; i < 100; ++i) {
						if (s.test(i)) {
							w[i / 64] |= std::uint64_t{ 1 } << (i % 64);
						}
					}
					a_intfc->WriteRecordData(w[0]);
					a_intfc->WriteRecordData(w[1]);
				}
			}
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			std::uint32_t type = 0, version = 0, length = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (version != kVersion) {
					continue;
				}
				switch (type) {
				case kRecPosters:
					g_posters = ReadIds(a_intfc);
					break;
				case kRecGhosts:
					g_staleGhosts = ReadIds(a_intfc);
					break;
				case kRecMerchants:
					{
						std::uint32_t n = 0;
						a_intfc->ReadRecordData(n);
						for (std::uint32_t k = 0; k < n; ++k) {
							RE::FormID    id = 0, resolved = 0;
							std::uint32_t period = 0;
							a_intfc->ReadRecordData(id);
							a_intfc->ReadRecordData(period);
							if (a_intfc->ResolveFormID(id, resolved)) {
								g_stocked[resolved] = period;
							}
						}
					}
					break;
				case kRecStash:
					{
						std::uint32_t n = 0;
						a_intfc->ReadRecordData(n);
						for (std::uint32_t k = 0; k < n; ++k) {
							std::uint64_t w[2]{};
							a_intfc->ReadRecordData(w[0]);
							a_intfc->ReadRecordData(w[1]);
							CardSet s;
							for (int i = 0; i < 100; ++i) {
								if (w[i / 64] & (std::uint64_t{ 1 } << (i % 64))) {
									s.set(i);
								}
							}
							g_stash.push_back(s);
						}
					}
					break;
				default:
					break;
				}
			}
			spdlog::info("Loaded: {} posters, {} rolled-up card sets", g_posters.size(), g_stash.size());
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			Placement::Reset();
			g_posters.clear();
			g_stash.clear();
			g_views.clear();
			g_staleGhosts.clear();
			g_stocked.clear();
			{
				std::lock_guard lk(g_evMtx);
				g_newCards.clear();
				g_returns.clear();
			}
			g_dirty = true;
		}

		template <class T>
		T* Lookup(RE::FormID a_id)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto* f = dh ? dh->LookupForm<T>(a_id, EspIds::kPlugin) : nullptr;
			if (!f) {
				spdlog::error("{}: form {:03X} not found", EspIds::kPlugin, a_id);
			}
			return f;
		}
	}

	// ====================================================================== public
	RE::TESObjectCONT* PosterForm() { return g_poster; }
	RE::TESObjectBOOK* RolledForm() { return g_rolled; }
	RE::TESObjectSTAT* GhostForm(bool a_red) { return a_red ? g_ghostRed : g_ghost; }

	int SehFilter(EXCEPTION_POINTERS* a_info, const char* a_where)
	{
		const auto code = a_info && a_info->ExceptionRecord ? a_info->ExceptionRecord->ExceptionCode : 0;
		const auto addr = a_info && a_info->ExceptionRecord ? reinterpret_cast<std::uintptr_t>(a_info->ExceptionRecord->ExceptionAddress) : 0;
		spdlog::critical("{} crashed (exception {:08X} at {:X}) - this part of Hearthcards is switched off until the game restarts", a_where, code, addr);
		return EXCEPTION_EXECUTE_HANDLER;
	}

	void Install()
	{
		auto* ser = SKSE::GetSerializationInterface();
		ser->SetUniqueID(kSaveId);
		ser->SetSaveCallback(OnSave);
		ser->SetLoadCallback(OnLoad);
		ser->SetRevertCallback(OnRevert);
	}

	bool OnDataLoaded()
	{
		g_poster = Lookup<RE::TESObjectCONT>(EspIds::kPoster);
		g_rolled = Lookup<RE::TESObjectBOOK>(EspIds::kPosterRolled);
		g_ghost = Lookup<RE::TESObjectSTAT>(EspIds::kGhost);
		g_ghostRed = Lookup<RE::TESObjectSTAT>(EspIds::kGhostRed);
		g_tiers[0] = Lookup<RE::TESLevItem>(EspIds::kListCommon);
		g_tiers[1] = Lookup<RE::TESLevItem>(EspIds::kListUncommon);
		g_tiers[2] = Lookup<RE::TESLevItem>(EspIds::kListRare);
		g_lootList = Lookup<RE::TESLevItem>(EspIds::kListLoot);
		g_vendorList = Lookup<RE::TESLevItem>(EspIds::kListVendor);
		g_drawerList = Lookup<RE::TESLevItem>(EspIds::kListDrawer);
		for (int i = 0; i < kCards; ++i) {
			g_cards[i] = Lookup<RE::TESObjectBOOK>(EspIds::kCardBase + i);
			if (g_cards[i]) {
				g_cardIndex[g_cards[i]->GetFormID()] = i;
			}
			g_slotNames[i] = fmt::format("Card{:03}", i + 1).c_str();
		}
		if (!g_poster || !g_rolled || !g_ghost || !g_ghostRed || !g_lootList || !g_vendorList || !g_drawerList || g_cardIndex.empty()) {
			spdlog::critical("Hearthcards.esp is missing or out of date - the mod is switched off");
			RE::DebugMessageBox("Hearthcards: Hearthcards.esp is missing or not enabled. The mod is switched off.");
			return false;
		}

		const auto& cfg = Config::Get();
		g_lootList->chanceNone = static_cast<std::int8_t>(100 - cfg.lootChance);
		g_drawerList->chanceNone = static_cast<std::int8_t>(100 - cfg.drawerChance);
		g_vendorList->chanceNone = static_cast<std::int8_t>(100 - cfg.vendorChance);
		int added = 0;
		if (cfg.loot) {
			for (const auto id : kLootLists) {
				added += Inject(id, g_lootList) ? 1 : 0;
			}
		}
		if (cfg.drawers) {
			added += Inject(kDrawerList, g_drawerList) ? 1 : 0;
		}
		spdlog::info("Cards added to {} vanilla leveled lists", added);
		if (auto* dh = RE::TESDataHandler::GetSingleton()) {
			for (auto* c : dh->GetFormArray<RE::TESObjectCONT>()) {
				if (!c) {
					continue;
				}
				for (std::uint32_t i = 0; i < c->numContainerObjects; ++i) {
					const auto* e = c->containerObjects[i];
					if (e && e->obj && e->obj->GetFormID() == kVendorList) {
						g_merchantChests.insert(c->GetFormID());
						break;
					}
				}
			}
		}
		spdlog::info("{} general goods merchant chests will carry {} cards", g_merchantChests.size(), cfg.vendors ? cfg.merchantCards : 0);

		REL::Relocation<std::uintptr_t> vt{ RE::VTABLE_TESObjectCONT[0] };
		g_origActivate = reinterpret_cast<ActivateFn>(vt.write_vfunc(0x37, reinterpret_cast<std::uintptr_t>(&ActivateHook)));

		if (auto* src = RE::ScriptEventSourceHolder::GetSingleton()) {
			src->AddEventSink<RE::TESContainerChangedEvent>(&g_containerSink);
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
		}
		g_ready = true;
		RebuildLists({});
		spdlog::info("{} cards ready", g_cardIndex.size());
		return true;
	}

	void OnGameLoaded()
	{
		g_dirty = true;
		for (auto& [id, v] : g_views) {
			v.known = false;
		}
	}

	void OnPosterPlaced(RE::TESObjectREFR* a_poster)
	{
		if (!a_poster) {
			return;
		}
		g_posters.push_back(a_poster->GetFormID());
		if (!g_stash.empty()) {
			const CardSet s = g_stash.back();
			g_stash.pop_back();
			for (int i = 0; i < kCards; ++i) {
				if (s.test(i) && g_cards[i]) {
					a_poster->AddObjectToContainer(g_cards[i], nullptr, 1, nullptr);
				}
			}
		}
		g_dirty = true;
	}

	void SetPosterHidden(RE::TESObjectREFR* a_poster, bool a_hidden)
	{
		if (!a_poster) {
			return;
		}
		g_views[a_poster->GetFormID()].hidden = a_hidden;
		if (auto* root = a_poster->Get3D()) {
			root->SetAppCulled(a_hidden);
		}
	}

	void Tick()
	{
		if (!g_ready) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->Get3D() || !player->GetParentCell()) {
			return;  // main menu / loading
		}
		if (!g_staleGhosts.empty()) {
			for (const auto id : g_staleGhosts) {
				if (auto* g = RE::TESForm::LookupByID<RE::TESObjectREFR>(id); g && !g->IsDeleted()) {
					g->Disable();
					g->SetDelete(true);
				}
			}
			g_staleGhosts.clear();
		}
		std::vector<int>    cards;
		std::vector<Return> returns;
		{
			std::lock_guard lk(g_evMtx);
			cards.swap(g_newCards);
			returns.swap(g_returns);
		}
		if (!returns.empty()) {
			ProcessReturns(returns);
		}
		if (!cards.empty()) {
			ProcessNewCards(cards);
		}
		const bool reread = g_dirty.exchange(false);
		if (reread) {
			CardSet owned = PinnedEverywhere();
			const auto have = CardCounts(player);
			for (int i = 0; i < kCards; ++i) {
				if (have[i] > 0) {
					owned.set(i);
				}
			}
			RebuildLists(owned);
		}
		RefreshViews(reread);
		Placement::Update();
	}
}

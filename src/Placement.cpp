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

#include "Placement.h"

#include "Cards.h"
#include "Config.h"

namespace Placement
{
	namespace
	{
		using UEFlag = RE::UserEvents::USER_EVENT_FLAG;

		constexpr float kHalfW = 40.0f;     // poster half width (game units, matches tools/make_assets.py)
		constexpr float kHalfH = 53.0f;     // poster half height
		constexpr float kWallGap = 0.6f;    // poster centre in front of the wall (half thickness + a hair)
		constexpr float kMaxWallZ = 0.35f;  // |normal.z| above this is a floor/ceiling, not a wall

		enum class Mode
		{
			kNone,
			kNew,
			kMove
		};

		Mode                g_mode = Mode::kNone;
		bool                g_wantNew = false;
		RE::ObjectRefHandle g_wantMove;
		RE::ObjectRefHandle g_target;  // the poster being moved
		RE::ObjectRefHandle g_ghost;
		RE::ObjectRefHandle g_ghostRed;
		bool                g_valid = false;
		RE::NiPoint3        g_pos;
		float               g_yaw = 0.0f;
		std::uint32_t       g_disabledControls = 0;
		int                 g_frames = 0;
		std::atomic<bool>   g_place{ false };
		std::atomic<bool>   g_cancel{ false };
		std::atomic<bool>   g_listening{ false };

		void Notify(const char* a_text) { RE::DebugNotification(a_text); }

		// ------------------------------------------------------------------ input
		class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				if (!g_listening || !a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				auto* ue = RE::UserEvents::GetSingleton();
				auto* cm = RE::ControlMap::GetSingleton();
				for (auto* e = *a_event; e; e = e->next) {
					auto* b = e->AsButtonEvent();
					if (!b || !b->IsDown()) {
						continue;
					}
					const auto  dev = b->GetDevice();
					const auto  id = b->GetIDCode();
					const auto& name = b->QUserEvent();
					if ((dev == RE::INPUT_DEVICE::kMouse && id == 1) || (dev == RE::INPUT_DEVICE::kKeyboard && id == 0x01) ||
						(ue && (name == ue->tweenMenu || name == ue->journal))) {
						g_cancel = true;  // right mouse, Esc, Tab / gamepad B
					} else if (ue && (name == ue->activate ||
										 (cm && (dev == RE::INPUT_DEVICE::kKeyboard || dev == RE::INPUT_DEVICE::kGamepad) &&
											 id == cm->GetMappedKey(ue->activate.c_str(), dev)))) {
						g_place = true;
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
		InputSink g_inputSink;

		// ------------------------------------------------------------------ raycasts
		struct Hit
		{
			RE::NiPoint3 point;
			RE::NiPoint3 normal;
			float        dist = 0.0f;
		};

		bool Raycast(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, Hit& a_out, RE::TESObjectREFR* a_ignore)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return false;
			}
			const float   s = RE::bhkWorld::GetWorldScale();
			std::uint32_t info = 0;
			player->GetCollisionFilterInfo(info);
			const std::uint32_t filter = (info & 0xFFFF0000) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);

			RE::NiPoint3 dir = a_to - a_from;
			const float  len = dir.Unitize();
			RE::NiPoint3 from = a_from;
			for (int tries = 0; tries < 4; ++tries) {
				RE::bhkPickData pick{};
				pick.rayInput.from = RE::hkVector4(from.x * s, from.y * s, from.z * s, 0.0f);
				pick.rayInput.to = RE::hkVector4(a_to.x * s, a_to.y * s, a_to.z * s, 0.0f);
				pick.rayInput.filterInfo = filter;
				world->PickObject(pick);
				if (!pick.rayOutput.HasHit()) {
					return false;
				}
				const RE::NiPoint3 p = from + (a_to - from) * pick.rayOutput.hitFraction;
				if (a_ignore && pick.rayOutput.rootCollidable) {
					if (RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable) == a_ignore) {
						from = p + dir * 2.0f;  // through the poster being moved
						if (a_from.GetDistance(from) >= len) {
							return false;
						}
						continue;
					}
				}
				float n[4];
				_mm_storeu_ps(n, pick.rayOutput.normal.quad);
				a_out.point = p;
				a_out.normal = { n[0], n[1], n[2] };
				a_out.normal.Unitize();
				a_out.dist = a_from.GetDistance(p);
				return true;
			}
			return false;
		}

		// The whole poster has wall behind it and nothing (floor, ceiling, shelf, corner) in the way.
		bool Fits(const RE::NiPoint3& a_c, const RE::NiPoint3& a_n, RE::TESObjectREFR* a_ignore)
		{
			RE::NiPoint3 right{ a_n.y, -a_n.x, 0.0f };
			if (right.Unitize() < 0.01f) {
				return false;
			}
			const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
			const float        hw = kHalfW * 0.92f;
			const float        hh = kHalfH * 0.92f;
			Hit                h;
			for (const float sx : { -1.0f, 1.0f }) {
				for (const float sy : { -1.0f, 1.0f }) {
					const RE::NiPoint3 q = a_c + right * (sx * hw) + up * (sy * hh);
					if (!Raycast(q + a_n * 10.0f, q - a_n * 10.0f, h, a_ignore) || std::abs(h.dist - 10.0f) > 4.0f ||
						h.normal * a_n < 0.85f) {
						return false;
					}
				}
			}
			const RE::NiPoint3 o = a_c + a_n * 3.0f;
			for (const auto& d : { right * kHalfW, right * -kHalfW, up * kHalfH, up * -kHalfH }) {
				if (Raycast(o, o + d, h, a_ignore)) {
					return false;
				}
			}
			return true;
		}

		RE::NiPoint3 CameraForward(const RE::NiCamera* a_cam)
		{
			// a NiCamera looks down its local X axis
			const auto&  r = a_cam->world.rotate;
			RE::NiPoint3 f{ r.entry[0][0], r.entry[1][0], r.entry[2][0] };
			f.Unitize();
			return f;
		}

		void MoveRef(RE::TESObjectREFR* a_ref, const RE::NiPoint3& a_pos, float a_yaw)
		{
			if (!a_ref) {
				return;
			}
			a_ref->data.angle = { 0.0f, 0.0f, a_yaw };
			a_ref->SetPosition(a_pos);
		}

		void ShowGhost(RE::TESObjectREFR* a_ref, bool a_visible)
		{
			if (auto* root = a_ref ? a_ref->Get3D() : nullptr) {
				if (root->GetAppCulled() == a_visible) {
					root->SetAppCulled(!a_visible);
				}
			}
		}

		void Aim()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cam = RE::Main::WorldRootCamera();
			if (!player || !cam) {
				return;
			}
			auto               target = g_target.get();
			const RE::NiPoint3 from = cam->world.translate;
			const RE::NiPoint3 dir = CameraForward(cam);
			const float        range = Config::Get().reach + from.GetDistance(player->GetPosition());

			Hit          h;
			RE::NiPoint3 n;
			g_valid = false;
			if (Raycast(from, from + dir * range, h, target.get())) {
				if (std::abs(h.normal.z) < kMaxWallZ) {
					n = { h.normal.x, h.normal.y, 0.0f };
					n.Unitize();
					g_pos = h.point + n * kWallGap;
					g_valid = Fits(h.point, n, target.get());
				} else {  // floor or ceiling: show it standing there, facing you
					n = { -dir.x, -dir.y, 0.0f };
					n.Unitize();
					g_pos = h.point + RE::NiPoint3{ 0.0f, 0.0f, kHalfH };
				}
			} else {
				n = { -dir.x, -dir.y, 0.0f };
				n.Unitize();
				g_pos = from + dir * (std::min)(range, 250.0f);
			}
			g_yaw = std::atan2(n.x, n.y);  // the poster's front (+Y) faces away from the wall

			auto green = g_ghost.get();
			auto red = g_ghostRed.get();
			MoveRef(green.get(), g_pos, g_yaw);
			MoveRef(red.get(), g_pos, g_yaw);
			ShowGhost(green.get(), g_valid);
			ShowGhost(red.get(), !g_valid);
		}

		// ------------------------------------------------------------------ start / end
		void SetControls(bool a_enable)
		{
			auto* cm = RE::ControlMap::GetSingleton();
			if (!cm) {
				return;
			}
			if (!a_enable) {
				g_disabledControls = 0;
				for (const auto f : { UEFlag::kActivate, UEFlag::kMenu, UEFlag::kFighting, UEFlag::kMainFour }) {
					if (cm->enabledControls.all(f)) {
						g_disabledControls |= static_cast<std::uint32_t>(f);
					}
				}
				if (g_disabledControls) {
					cm->ToggleControls(static_cast<UEFlag>(g_disabledControls), false);
				}
			} else if (g_disabledControls) {
				cm->ToggleControls(static_cast<UEFlag>(g_disabledControls), true);
				g_disabledControls = 0;
			}
		}

		void DeleteRef(RE::ObjectRefHandle& a_handle)
		{
			if (auto ref = a_handle.get()) {
				ref->Disable();
				ref->SetDelete(true);
			}
			a_handle.reset();
		}

		void End()
		{
			DeleteRef(g_ghost);
			DeleteRef(g_ghostRed);
			g_target.reset();
			SetControls(true);
			g_listening = false;
			g_mode = Mode::kNone;
		}

		int RolledCount()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* rolled = Cards::RolledForm();
			if (!player || !rolled) {
				return 0;
			}
			const auto counts = player->GetInventoryCounts([rolled](RE::TESBoundObject& a_obj) { return &a_obj == rolled; });
			int        n = 0;
			for (const auto& [obj, c] : counts) {
				n += c;
			}
			return n;
		}

		void Begin(Mode a_mode, RE::ObjectRefHandle a_target)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell() || !player->GetParentCell()->GetbhkWorld()) {
				return;
			}
			if (a_mode == Mode::kNew && RolledCount() < 1) {
				return;
			}
			auto target = a_target.get();
			if (a_mode == Mode::kMove && !target) {
				return;
			}
			auto green = player->PlaceObjectAtMe(Cards::GhostForm(false), false);
			auto red = player->PlaceObjectAtMe(Cards::GhostForm(true), false);
			if (!green || !red) {
				spdlog::error("Placement: could not place the preview");
				if (green) {
					green->Disable();
					green->SetDelete(true);
				}
				if (red) {
					red->Disable();
					red->SetDelete(true);
				}
				return;
			}
			g_ghost = green->GetHandle();
			g_ghostRed = red->GetHandle();
			g_target = a_target;
			if (a_mode == Mode::kMove) {
				Cards::SetPosterHidden(target.get(), true);
			}
			g_mode = a_mode;
			g_frames = 0;
			g_valid = false;
			g_place = false;
			g_cancel = false;
			g_listening = true;
			SetControls(false);
			Notify("Aim at a wall. Activate hangs the poster; right-click or Esc cancels.");
		}

		void Commit()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (g_mode == Mode::kNew) {
				if (RolledCount() < 1) {
					End();
					return;
				}
				auto ref = player->PlaceObjectAtMe(Cards::PosterForm(), true);
				if (!ref) {
					Notify("The poster could not be hung here.");
					return;
				}
				MoveRef(ref.get(), g_pos, g_yaw);
				player->RemoveItem(Cards::RolledForm(), 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				Cards::OnPosterPlaced(ref.get());
				spdlog::info("Poster {:08X} hung at ({:.0f}, {:.0f}, {:.0f})", ref->GetFormID(), g_pos.x, g_pos.y, g_pos.z);
			} else if (auto target = g_target.get()) {
				MoveRef(target.get(), g_pos, g_yaw);
				Cards::SetPosterHidden(target.get(), false);
				spdlog::info("Poster {:08X} moved to ({:.0f}, {:.0f}, {:.0f})", target->GetFormID(), g_pos.x, g_pos.y, g_pos.z);
			}
			End();
		}
	}

	void Install()
	{
		if (auto* mgr = RE::BSInputDeviceManager::GetSingleton()) {
			mgr->AddEventSink<RE::InputEvent*>(&g_inputSink);
		}
	}

	void RequestNew() { g_wantNew = true; }

	void RequestMove(RE::ObjectRefHandle a_poster) { g_wantMove = a_poster; }

	bool Active() { return g_mode != Mode::kNone; }

	std::vector<RE::FormID> GhostIds()
	{
		std::vector<RE::FormID> out;
		for (auto* h : { &g_ghost, &g_ghostRed }) {
			if (auto ref = h->get()) {
				out.push_back(ref->GetFormID());
			}
		}
		return out;
	}

	void Cancel()
	{
		g_wantNew = false;
		g_wantMove.reset();
		if (g_mode == Mode::kNone) {
			return;
		}
		if (g_mode == Mode::kMove) {
			if (auto target = g_target.get()) {
				Cards::SetPosterHidden(target.get(), false);
			}
		}
		End();
	}

	void Reset()
	{
		g_wantNew = false;
		g_wantMove.reset();
		g_ghost.reset();
		g_ghostRed.reset();
		g_target.reset();
		SetControls(true);
		g_listening = false;
		g_mode = Mode::kNone;
	}

	void Update()
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui) {
			return;
		}
		const bool paused = ui->GameIsPaused() || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		if (g_mode == Mode::kNone) {
			if (paused) {
				return;
			}
			if (g_wantNew) {
				g_wantNew = false;
				Begin(Mode::kNew, {});
			} else if (g_wantMove) {
				auto h = g_wantMove;
				g_wantMove.reset();
				Begin(Mode::kMove, h);
			}
			return;
		}
		if (paused) {  // journal, console, a loading screen...
			Cancel();
			return;
		}
		++g_frames;
		bool place = g_place.exchange(false);
		bool cancel = g_cancel.exchange(false);
		if (g_frames < 10) {  // the click that started placement
			place = cancel = false;
		}
		if (cancel) {
			Cancel();
			Notify("Poster placement canceled.");
			return;
		}
		Aim();
		if (place) {
			if (g_valid) {
				Commit();
			} else {
				Notify("The poster needs a clear, flat wall within reach.");
			}
		}
	}
}

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

// Hanging works like Hearthframe's picture frames: the preview sits where the crosshair meets any
// roughly vertical surface - no checks for corners or room around it - and you can push it into or
// out from the wall and change its size while aiming.
namespace Placement
{
	namespace
	{
		using UEFlag = RE::UserEvents::USER_EVENT_FLAG;
		using MouseKey = RE::BSWin32MouseDevice::Key;

		constexpr std::uint32_t kDIK_Escape = 0x01;
		constexpr std::uint32_t kDIK_Up = 0xC8;
		constexpr std::uint32_t kDIK_Down = 0xD0;

		constexpr float kHalfThickness = 0.5f;  // the poster's origin is the middle of its 1-unit board
		constexpr float kDefaultOffset = 0.3f;  // gap between the wall and the poster's back
		constexpr float kMinOffset = -40.0f;    // wall collision often sits well in front of the visible wall
		constexpr float kMaxOffset = 12.0f;
		constexpr float kMinScale = 0.3f;
		constexpr float kMaxScale = 2.0f;

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
		RE::ObjectRefHandle g_ghostRed;  // older saves may still hold one; never shown now
		bool                g_valid = false;
		bool                g_ghostShown = false;
		bool                g_ghostReady = false;
		RE::NiPoint3        g_pos;
		float               g_yaw = 0.0f;
		float               g_offset = kDefaultOffset;
		float               g_scale = 1.0f;
		const char*         g_whyNot = nullptr;
		RE::TESObjectCELL*  g_cell = nullptr;
		RE::TESWorldSpace*  g_world = nullptr;
		std::uint32_t       g_disabledControls = 0;
		int                 g_frames = 0;
		std::atomic<bool>   g_place{ false };
		std::atomic<bool>   g_cancel{ false };
		std::atomic<int>    g_offsetSteps{ 0 };
		std::atomic<int>    g_scaleSteps{ 0 };
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
					if (dev == RE::INPUT_DEVICE::kMouse) {
						if (id == MouseKey::kLeftButton) {
							g_place = true;
						} else if (id == MouseKey::kRightButton) {
							g_cancel = true;
						} else if (id == MouseKey::kWheelUp) {
							++g_scaleSteps;
						} else if (id == MouseKey::kWheelDown) {
							--g_scaleSteps;
						}
						continue;
					}
					if (dev == RE::INPUT_DEVICE::kKeyboard) {
						if (id == kDIK_Escape) {
							g_cancel = true;
							continue;
						}
						if (id == kDIK_Up) {
							--g_offsetSteps;  // towards / into the wall
							continue;
						}
						if (id == kDIK_Down) {
							++g_offsetSteps;  // away from the wall
							continue;
						}
					}
					if (ue && (name == ue->tweenMenu || name == ue->journal)) {
						g_cancel = true;  // Tab / gamepad B
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

		// ------------------------------------------------------------------ aiming (same ray as Hearthframe)
		struct Hit
		{
			RE::NiPoint3       pos;
			RE::NiPoint3       normal;
			RE::TESObjectREFR* ref = nullptr;
		};

		std::optional<Hit> RaycastFromCamera(float a_reach, RE::TESObjectREFR* a_ignore)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world || !camera || !camera->cameraRoot) {
				return std::nullopt;
			}
			const auto&  xf = camera->cameraRoot->world;
			RE::NiPoint3 from = xf.translate;
			RE::NiPoint3 dir{ xf.rotate.entry[0][1], xf.rotate.entry[1][1], xf.rotate.entry[2][1] };
			if (dir.Unitize() < 1e-4f) {
				return std::nullopt;
			}
			// In third person the camera sits behind the player; reach is measured from the player.
			const RE::NiPoint3 p = player->GetPosition();
			const float camToPlayer = std::sqrt((p.x - from.x) * (p.x - from.x) + (p.y - from.y) * (p.y - from.y));
			const RE::NiPoint3 to = from + dir * (a_reach + camToPlayer);

			const float   s = RE::bhkWorld::GetWorldScale();
			RE::CFilter info{};
			player->GetCollisionFilterInfo(info);
			const std::uint32_t filter = (info.filter & 0xFFFF0000) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			for (int tries = 0; tries < 4; ++tries) {
				RE::bhkPickData pick{};
				pick.rayInput.from = RE::hkVector4(from.x * s, from.y * s, from.z * s, 0.0f);
				pick.rayInput.to = RE::hkVector4(to.x * s, to.y * s, to.z * s, 0.0f);
				pick.rayInput.filterInfo = filter;
				world->PickObject(pick);
				if (!pick.rayOutput.HasHit()) {
					return std::nullopt;
				}
				Hit out;
				out.pos = from + (to - from) * pick.rayOutput.hitFraction;
				if (pick.rayOutput.rootCollidable) {
					out.ref = RE::TESHavokUtilities::FindCollidableRef(*pick.rayOutput.rootCollidable);
				}
				if (a_ignore && out.ref == a_ignore) {  // the poster being moved: look through it
					from = out.pos + dir * 2.0f;
					continue;
				}
				alignas(16) float n[4];
				_mm_store_ps(n, pick.rayOutput.normal.quad);
				out.normal = { n[0], n[1], n[2] };
				return out;
			}
			return std::nullopt;
		}

		// Where the poster would hang right now; g_whyNot says why not.
		bool ComputePose()
		{
			auto       target = g_target.get();
			const auto hit = RaycastFromCamera(Config::Get().reach, target.get());
			if (!hit) {
				g_whyNot = "Nothing in reach - aim at a wall closer to you.";
				return false;
			}
			if (hit->ref && hit->ref != target.get() && hit->ref->Is(RE::FormType::ActorCharacter)) {
				g_whyNot = "You can't hang a poster on someone.";
				return false;
			}
			const auto& nrm = hit->normal;
			const float nlen = std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y);
			if (std::fabs(nrm.z) > 0.6f || nlen < 0.3f) {
				g_whyNot = "That's not a wall - aim at a vertical surface.";
				return false;
			}
			const float nx = nrm.x / nlen;
			const float ny = nrm.y / nlen;
			const float out = kHalfThickness * g_scale + g_offset;
			g_pos = { hit->pos.x + nx * out, hit->pos.y + ny * out, hit->pos.z };
			g_yaw = std::atan2(nx, ny);  // local +Y (the poster's front) faces away from the wall
			return true;
		}

		void SetRefScale(RE::TESObjectREFR* a_ref, float a_scale)
		{
			if (!a_ref) {
				return;
			}
			a_ref->GetReferenceRuntimeData().refScale = static_cast<std::uint16_t>(std::lround(a_scale * 100.0f));
			a_ref->AddChange(1u << 4);  // CHANGE_REFR_SCALE - kept in the save
			if (auto* root = a_ref->Get3D()) {
				root->local.scale = a_scale;
				RE::NiUpdateData ud{};
				root->Update(ud);
			}
		}

		void ShowGhost(bool a_visible)
		{
			auto ghost = g_ghost.get();
			auto* root = ghost ? ghost->Get3D() : nullptr;
			if (root && g_ghostShown != a_visible) {
				root->SetAppCulled(!a_visible);
				g_ghostShown = a_visible;
			}
		}

		void UpdateGhost()
		{
			auto  ghost = g_ghost.get();
			auto* root = ghost ? ghost->Get3D() : nullptr;
			if (!root) {
				return;  // still loading
			}
			if (!g_ghostReady) {
				root->SetCollisionLayer(RE::COL_LAYER::kNonCollidable);  // the aiming ray must not hit the preview
				root->SetAppCulled(true);
				g_ghostShown = false;
				g_ghostReady = true;
			}
			if (!g_valid) {
				ShowGhost(false);
				return;
			}
			// Same as Papyrus SetPosition/SetAngle: update the reference, let the game move its 3D.
			ghost->data.location = g_pos;
			ghost->data.angle = { 0.0f, 0.0f, g_yaw };
			ghost->Update3DPosition(true);
			if (std::fabs(root->local.scale - g_scale) > 0.001f) {
				SetRefScale(ghost.get(), g_scale);
			}
			ShowGhost(true);
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
				for (const auto f : { UEFlag::kActivate, UEFlag::kMenu, UEFlag::kFighting, UEFlag::kMainFour, UEFlag::kWheelZoom }) {
					if (cm->AreControlsEnabled(f)) {
						g_disabledControls |= static_cast<std::uint32_t>(f);
					}
				}
				if (g_disabledControls) {
					cm->ToggleControls(static_cast<UEFlag>(g_disabledControls), false, false);
				}
			} else if (g_disabledControls) {
				cm->ToggleControls(static_cast<UEFlag>(g_disabledControls), true, false);
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
			const float h = player->GetAngleZ();
			const RE::NiPoint3 p = player->GetPosition();
			const RE::NiPoint3 at{ p.x + std::sin(h) * 150.0f, p.y + std::cos(h) * 150.0f, p.z + 100.0f };
			auto ghost = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(Cards::GhostForm(false), at, { 0.0f, 0.0f, h },
				player->GetParentCell(), player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), false, true);
			if (!ghost.get()) {
				spdlog::error("Placement: could not place the preview");
				return;
			}
			g_ghost = ghost;
			g_ghostReady = false;
			g_ghostShown = false;
			g_target = a_target;
			g_scale = 1.0f;
			g_offset = kDefaultOffset;
			if (a_mode == Mode::kMove) {
				g_scale = std::clamp(target->GetReferenceRuntimeData().refScale / 100.0f, kMinScale, kMaxScale);
				Cards::SetPosterHidden(target.get(), true);
			}
			g_cell = player->GetParentCell();
			g_world = player->GetWorldspace();
			g_mode = a_mode;
			g_frames = 0;
			g_valid = false;
			g_place = false;
			g_cancel = false;
			g_offsetSteps = 0;
			g_scaleSteps = 0;
			g_listening = true;
			SetControls(false);
			Notify("Aim at a wall: left-click or Activate hangs the poster. Wheel: size, Up/Down: distance from the wall. Right-click/Esc cancels.");
		}

		void Commit()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			const RE::NiPoint3 angle{ 0.0f, 0.0f, g_yaw };
			if (g_mode == Mode::kNew) {
				if (RolledCount() < 1) {
					End();
					return;
				}
				auto handle = RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(Cards::PosterForm(), g_pos, angle,
					player->GetParentCell(), player->GetWorldspace(), nullptr, nullptr, RE::ObjectRefHandle(), true, true);
				auto ref = handle.get();
				if (!ref) {
					Notify("The poster could not be hung here.");
					spdlog::error("CreateReferenceAtLocation failed for the poster");
					return;
				}
				if (std::fabs(g_scale - 1.0f) > 0.001f) {
					SetRefScale(ref.get(), g_scale);
				}
				player->RemoveItem(Cards::RolledForm(), 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
				Cards::OnPosterPlaced(ref.get());
				spdlog::info("Poster {:08X} hung at ({:.0f}, {:.0f}, {:.0f}), {:.0f}%", ref->GetFormID(), g_pos.x, g_pos.y, g_pos.z, g_scale * 100.0f);
			} else if (auto target = g_target.get()) {
				target->data.angle = angle;
				target->SetPosition(g_pos);
				SetRefScale(target.get(), g_scale);
				Cards::SetPosterHidden(target.get(), false);
				spdlog::info("Poster {:08X} moved to ({:.0f}, {:.0f}, {:.0f}), {:.0f}%", target->GetFormID(), g_pos.x, g_pos.y, g_pos.z, g_scale * 100.0f);
			}
			End();
			Notify("Poster hung.");
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
		// went through a door (interior <-> exterior, or another worldspace)
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			auto* cell = player->GetParentCell();
			if (cell && cell != g_cell && (cell->IsInteriorCell() || (g_cell && g_cell->IsInteriorCell()) || player->GetWorldspace() != g_world)) {
				Cancel();
				Notify("Hanging canceled - you left the area.");
				return;
			}
		}
		++g_frames;
		bool place = g_place.exchange(false);
		bool cancel = g_cancel.exchange(false);
		const int offsetSteps = g_offsetSteps.exchange(0);
		const int scaleSteps = g_scaleSteps.exchange(0);
		if (g_frames < 10) {  // the click that started placement
			place = cancel = false;
		}
		if (cancel) {
			Cancel();
			Notify("Poster placement canceled.");
			return;
		}
		if (offsetSteps) {
			g_offset = std::clamp(g_offset + offsetSteps * 1.0f, kMinOffset, kMaxOffset);
			RE::DebugNotification(fmt::format("Distance from wall: {:+.0f}", g_offset - kDefaultOffset).c_str());
		}
		if (scaleSteps) {
			g_scale = std::clamp(g_scale + scaleSteps * 0.05f, kMinScale, kMaxScale);
			RE::DebugNotification(fmt::format("Poster size: {:.0f}%", g_scale * 100.0f).c_str());
		}
		g_whyNot = nullptr;
		g_valid = ComputePose();
		UpdateGhost();
		if (place) {
			if (g_valid) {
				Commit();
			} else {
				Notify(g_whyNot ? g_whyNot : "Can't hang the poster here.");
			}
		}
	}
}

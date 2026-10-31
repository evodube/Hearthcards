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
#include "Placement.h"

namespace
{
	void SetupLog()
	{
		auto path = SKSE::log::log_directory();
		if (!path) {
			return;
		}
		*path /= "Hearthcards.log";
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
	}

	// ------------------------------------------------------------------ once per frame (Present hook, as in Hearthframe)
	using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
	PresentFn         g_originalPresent = nullptr;
	std::atomic<bool> g_tickDisabled{ false };
	std::atomic<bool> g_tickQueued{ false };

	void SafeTick()
	{
		__try {
			Cards::Tick();
		} __except (Cards::SehFilter(GetExceptionInformation(), "Hearthcards tick")) {
			g_tickDisabled = true;
		}
	}

	HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* a_sc, UINT a_sync, UINT a_flags)
	{
		// game logic runs as a main-thread task, once per frame
		if (!g_tickDisabled && !g_tickQueued.exchange(true)) {
			SKSE::GetTaskInterface()->AddTask([] {
				g_tickQueued = false;
				if (!g_tickDisabled) {
					SafeTick();
				}
			});
		}
		return g_originalPresent(a_sc, a_sync, a_flags);
	}

	bool PatchPresentSlot(IDXGISwapChain* a_sc)
	{
		auto** vtable = *reinterpret_cast<void***>(a_sc);
		if (vtable[8] == reinterpret_cast<void*>(&HookedPresent)) {
			return true;
		}
		DWORD old = 0;
		if (!::VirtualProtect(&vtable[8], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
			spdlog::error("VirtualProtect failed ({})", ::GetLastError());
			return false;
		}
		g_originalPresent = reinterpret_cast<PresentFn>(vtable[8]);
		vtable[8] = reinterpret_cast<void*>(&HookedPresent);
		::VirtualProtect(&vtable[8], sizeof(void*), old, &old);
		return true;
	}

	bool SafeInstallPresentHook()
	{
		__try {
			auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
			auto* sc = renderer ? reinterpret_cast<IDXGISwapChain*>(renderer->data.renderWindows[0].swapChain) : nullptr;
			return sc && PatchPresentSlot(sc);
		} __except (Cards::SehFilter(GetExceptionInformation(), "InstallPresentHook")) {
			return false;
		}
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kDataLoaded:
			if (Cards::OnDataLoaded()) {
				Placement::Install();
				if (SafeInstallPresentHook()) {
					spdlog::info("Present hook installed");
				} else {
					spdlog::error("Could not install the Present hook - posters will not work");
				}
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
		case SKSE::MessagingInterface::kNewGame:
			Cards::OnGameLoaded();
			break;
		default:
			break;
		}
	}

	// Which game this is, and whether Address Library for it is installed (the usual cause of an AE/GOG load failure).
	void LogEnvironment()
	{
		const auto ver = REL::Module::get().version();
		spdlog::info("Game version {} ({})", ver.string("."sv), REL::Module::IsAE() ? "AE" : (REL::Module::IsSE() ? "SE" : "other"));
		const auto lib = REL::Module::IsAE() ? fmt::format("Data\\SKSE\\Plugins\\versionlib-{}.bin", ver.string()) :
		                                       fmt::format("Data\\SKSE\\Plugins\\version-{}.bin", ver.string());
		std::error_code ec;
		if (std::filesystem::exists(lib, ec)) {
			spdlog::info("Address Library found: {}", lib);
		} else {
			spdlog::critical("Address Library NOT FOUND: {} - install 'Address Library for SKSE Plugins' for this game version", lib);
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SetupLog();
	spdlog::info("Hearthcards {} loaded, game {}", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."sv),
		REL::Module::get().version().string("."sv));
	LogEnvironment();
	Config::Load();
	Cards::Install();
	if (!SKSE::GetMessagingInterface()->RegisterListener(OnMessage)) {
		spdlog::error("Could not register with the SKSE messaging interface");
		return false;
	}
	return true;
}

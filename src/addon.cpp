#include "Nexus.h"
#include "imgui.h"

#include "live_data_api.hpp"
#include "settings.hpp"
#include "tonic.hpp"
#include "ui.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

AddonAPI* api = nullptr;
AddonDefinition addon {};
std::chrono::steady_clock::time_point loadedAt {};
std::chrono::steady_clock::time_point nextLiveInitAttempt {};
std::uint32_t liveInitAttempts = 0;
std::atomic_bool stopping = false;
std::atomic_uint32_t renderCallsInFlight = 0;
bool liveInitialized = false;

constexpr auto liveHookDelay = std::chrono::seconds {5};
constexpr auto liveRetryDelaySeconds = 5;
constexpr auto unloadDrainTimeout = std::chrono::seconds {5};

void tryInitializeLive()
{
    if (api == nullptr || liveInitialized) return;
    const auto now = std::chrono::steady_clock::now();
    if (now - loadedAt < liveHookDelay) return;
    if (now < nextLiveInitAttempt) return;
    ++liveInitAttempts;
    const bool ready = voxtonic::live_data::initialize(api);
    if (ready) {
        liveInitialized = true;
        api->Log(ELogLevel_INFO, "VoxTonic", "Live transformation snapshots enabled.");
        return;
    }
    nextLiveInitAttempt = now + std::chrono::seconds {liveRetryDelaySeconds};
    if (liveInitAttempts == 1 || liveInitAttempts % 5 == 0) {
        api->Log(ELogLevel_WARNING, "VoxTonic",
            "Live snapshot source unavailable; will retry shortly.");
    }
}

struct RenderScope {
    RenderScope() { renderCallsInFlight.fetch_add(1, std::memory_order_acq_rel); }
    ~RenderScope() { renderCallsInFlight.fetch_sub(1, std::memory_order_acq_rel); }
};

void render()
{
    RenderScope scope;
    if (stopping.load(std::memory_order_acquire)) return;
    tryInitializeLive();
    voxtonic::live_data::pump();
    voxtonic::tonic::tick(api, nullptr);
    voxtonic::ui::render();
}

void renderOptions()
{
    RenderScope scope;
    if (stopping.load(std::memory_order_acquire)) return;
    voxtonic::ui::renderOptions();
}

void load(AddonAPI* addonApi)
{
    api = addonApi;
    loadedAt = std::chrono::steady_clock::now();
    stopping.store(false, std::memory_order_release);
    renderCallsInFlight.store(0, std::memory_order_release);
    liveInitialized = false;
    liveInitAttempts = 0;
    nextLiveInitAttempt = {};
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(api->ImguiContext));
    ImGui::SetAllocatorFunctions(
        reinterpret_cast<void*(*)(size_t, void*)>(reinterpret_cast<std::uintptr_t>(api->ImguiMalloc)),
        reinterpret_cast<void(*)(void*, void*)>(reinterpret_cast<std::uintptr_t>(api->ImguiFree)));
    voxtonic::settings::load();
    voxtonic::ui::setApi(api);
    voxtonic::tonic::reset();
    voxtonic::tonic::updateBindings(api);
    api->Renderer.Register(ERenderType_Render, render);
    api->Renderer.Register(ERenderType_OptionsRender, renderOptions);
    api->Log(ELogLevel_INFO, "VoxTonic", "Loaded.");
}

void unload()
{
    voxtonic::settings::saveIfChanged(true);
    stopping.store(true, std::memory_order_release);
    if (api != nullptr) {
        api->Renderer.Deregister(render);
        api->Renderer.Deregister(renderOptions);
    }
    const auto deadline = std::chrono::steady_clock::now() + unloadDrainTimeout;
    while (renderCallsInFlight.load(std::memory_order_acquire) != 0
           && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    voxtonic::live_data::shutdown();
    voxtonic::tonic::updateBindings(nullptr);
    voxtonic::tonic::reset();
    voxtonic::ui::setApi(nullptr);
    api = nullptr;
    stopping.store(false, std::memory_order_release);
    liveInitialized = false;
}

extern "C" __declspec(dllexport) AddonDefinition* GetAddonDef()
{
    addon.Signature = -0x564F58;  // "VOX" — negative: not on Raidcore
    addon.APIVersion = NEXUS_API_VERSION;
    addon.Name = "VoxTonic";
    addon.Version = {VOXT_VERSION_MAJOR, VOXT_VERSION_MINOR,
        VOXT_VERSION_PATCH, VOXT_VERSION_BUILD};
    addon.Author = "Vox";
    addon.Description = "Cosmetic tonic auto re-press.";
    addon.Load = load;
    addon.Unload = unload;
    addon.Flags = EAddonFlags_IsVolatile;
    addon.Provider = EUpdateProvider_GitHub;
    addon.UpdateLink = "https://api.github.com/repos/JordiBrisbois/VoxTonic/releases/latest";
    return &addon;
}

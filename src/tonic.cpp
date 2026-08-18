#include "tonic.hpp"

#include "Nexus.h"
#include "companion.hpp"
#include "live_data_api.hpp"
#include "mumble_link.hpp"
#include "settings.hpp"
#include "tonic_ids.hpp"
#include "tonic_logic.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace voxtonic::tonic {
namespace {

constexpr auto tickInterval = std::chrono::milliseconds {100};
constexpr int bindHoldMs = 50;
constexpr int kMountToggleBind = 152;
constexpr auto mountUnequipSettle = std::chrono::milliseconds {250};
constexpr auto mountAttemptTimeout = std::chrono::milliseconds {1500};
constexpr auto echoGuardWindow = std::chrono::milliseconds {180};

logic::DecisionState decisionState;
std::chrono::steady_clock::time_point lastTick {};
AddonAPI* boundApi = nullptr;

std::atomic_bool mountRequested = false;
std::atomic_bool mountPressSent = false;
std::chrono::steady_clock::time_point mountRequestedAt {};
std::chrono::steady_clock::time_point lastProgrammaticPressAt {};
WPARAM lastProgrammaticKey = 0;
std::vector<std::uint32_t> lastTrackedIds;
bool trackedIdsInitialized = false;
bool decisionFeatureWasActive = false;

bool isTransformed()
{
    const auto active = live_data::activeEffectIds();
    if (settings::scanAll) {
        return std::ranges::any_of(ids::kKnownTonicIds, [&](const std::uint32_t id) {
            return std::ranges::find(active, id) != active.end();
        });
    }
    return std::ranges::find(active, settings::effectId) != active.end();
}

void syncTrackedIds()
{
    static bool lastScanAll = false;
    static std::uint32_t lastEffectId = 0;
    if (trackedIdsInitialized
        && lastScanAll == settings::scanAll
        && lastEffectId == settings::effectId) return;

    std::vector<std::uint32_t> tracked;
    if (settings::scanAll) {
        tracked.assign(ids::kKnownTonicIds.begin(), ids::kKnownTonicIds.end());
    } else if (settings::effectId != 0) {
        tracked.push_back(settings::effectId);
    }
    if (tracked == lastTrackedIds) {
        trackedIdsInitialized = true;
        return;
    }
    lastScanAll = settings::scanAll;
    lastEffectId = settings::effectId;
    lastTrackedIds = std::move(tracked);
    trackedIdsInitialized = true;
    live_data::setTrackedIds(lastTrackedIds);
}

bool pressGameBind(AddonAPI* api, const int bind)
{
    if (api == nullptr || api->GameBinds.InvokeAsync == nullptr) return false;
    api->GameBinds.InvokeAsync(static_cast<EGameBinds>(bind), bindHoldMs);
    lastProgrammaticPressAt = std::chrono::steady_clock::now();
    lastProgrammaticKey = static_cast<WPARAM>(settings::mountUnlockKey);
    return true;
}

UINT onMountUnlockWndProc(HWND, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    if (companion::isActive()) return 1;
    if (uMsg != WM_KEYDOWN && uMsg != WM_SYSKEYDOWN) return 1;
    if (wParam != static_cast<WPARAM>(settings::mountUnlockKey)) return 1;
    if ((lParam & 0x40000000) != 0) return 1;
    if (boundApi == nullptr) return 1;
    if (!settings::enabled) return 1;
    if (!settings::mountUnlockEnabled) return 1;
    if (!settings::enablePve) return 1;
    if (mumble_link::textInputFocused()) return 1;

    const auto now = std::chrono::steady_clock::now();
    if (lastProgrammaticPressAt != std::chrono::steady_clock::time_point {}
        && now - lastProgrammaticPressAt < echoGuardWindow
        && wParam == lastProgrammaticKey) {
        return 0;
    }

    if (mumble_link::isCompetitive()) return 1;
    if (mumble_link::mountIndex() != 0) return 1;
    if (!isTransformed()) return 1;

    if (!pressGameBind(boundApi, settings::noveltyBind)) return 1;
    mountRequested.store(true, std::memory_order_release);
    mountPressSent.store(false, std::memory_order_release);
    mountRequestedAt = now;
    return 0;
}

}

void reset()
{
    decisionState = {};
    lastTick = {};
    mountRequested.store(false, std::memory_order_release);
    mountPressSent.store(false, std::memory_order_release);
    mountRequestedAt = {};
    lastProgrammaticPressAt = {};
    lastProgrammaticKey = 0;
    lastTrackedIds.clear();
    trackedIdsInitialized = false;
    decisionFeatureWasActive = false;
}

void updateBindings(void* apiRaw)
{
    auto* api = static_cast<AddonAPI*>(apiRaw);
    if (boundApi != nullptr && boundApi->WndProc.Deregister != nullptr) {
        boundApi->WndProc.Deregister(onMountUnlockWndProc);
    }
    boundApi = api;
    if (api == nullptr) return;
    if (!settings::mountUnlockEnabled) return;
    if (api->WndProc.Register == nullptr) return;
    api->WndProc.Register(onMountUnlockWndProc);
}

void tick(void* apiRaw, void*)
{
    auto* api = static_cast<AddonAPI*>(apiRaw);
    if (api == nullptr) return;
    companion::poll();
    if (companion::isActive()) {
        mountRequested.store(false, std::memory_order_release);
        decisionFeatureWasActive = false;
        decisionState = {};
        return;
    }

    syncTrackedIds();

    if (!settings::enabled) {
        decisionFeatureWasActive = false;
        decisionState = {};
        mountRequested.store(false, std::memory_order_release);
        return;
    }
    if (!live_data::ready()) return;

    const auto now = std::chrono::steady_clock::now();

    const bool hasMountRequest = mountRequested.load(std::memory_order_acquire);
    if (!hasMountRequest) {
        if (lastTick != std::chrono::steady_clock::time_point {}
            && now - lastTick < tickInterval) return;
        lastTick = now;
    } else {
        lastTick = now;
    }

    const bool modeEnabled = mumble_link::isCompetitive()
        ? settings::enableCompetitive : settings::enablePve;
    if (!modeEnabled) {
        decisionFeatureWasActive = false;
        decisionState = {};
        mountRequested.store(false, std::memory_order_release);
        return;
    }
    if (!decisionFeatureWasActive) {
        // A setting toggle is an explicit re-enable. Start with the same
        // safety checks as normal operation, but do not make the user wait
        // through the addon-startup grace period again.
        decisionState = {};
        decisionState.started = true;
        decisionState.startedAt = now - std::chrono::milliseconds {2000};
        decisionState.lastActiveAt = now - std::chrono::milliseconds {300};
        decisionFeatureWasActive = true;
    }

    const bool transformed = isTransformed();
    const bool mounted = mumble_link::mountIndex() != 0;

    if (hasMountRequest) {
        if (mounted) {
            mountRequested.store(false, std::memory_order_release);
            return;
        }
        if (now - mountRequestedAt > mountAttemptTimeout) {
            mountRequested.store(false, std::memory_order_release);
        } else if (!mountPressSent.load(std::memory_order_acquire)) {
            // Mirror VoxSake: only press the mount bind once the transformation
            // is really gone (snapshot) and the settle time has passed. No
            // forced press: a snapshot lag would fire the mount key while still
            // transformed, and the key is swallowed by the game.
            if (now - mountRequestedAt >= mountUnequipSettle && !transformed) {
                if (pressGameBind(api, kMountToggleBind)) {
                    mountPressSent.store(true, std::memory_order_release);
                } else {
                    mountRequested.store(false, std::memory_order_release);
                }
                return;
            }
            return;
        } else {
            return;
        }
    }

    logic::DecisionParams params;
    params.rePressDelay = std::chrono::milliseconds {settings::rePressDelayMs};
    if (!logic::decideShouldPress(transformed, mounted, now, params, decisionState)) {
        return;
    }

    pressGameBind(api, settings::noveltyBind);
}

}

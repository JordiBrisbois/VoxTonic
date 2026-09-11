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
#include <mutex>
#include <optional>

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

// The WndProc callback runs on the game's window thread while everything else
// here runs on the Nexus render thread. Cross-thread state is therefore
// atomic or mutex-guarded; raw settings globals are never read from the
// window thread — tick()/updateBindings() publish atomic snapshots.
std::atomic<AddonAPI*> boundApi {nullptr};

struct WndSettings {
    std::atomic<int> mountKey {0};
    std::atomic<bool> enabled {false};
    std::atomic<bool> unlockEnabled {false};
    std::atomic<bool> pveEnabled {false};
};
WndSettings wnd;

void refreshWndSettings()
{
    wnd.mountKey.store(settings::mountUnlockKey, std::memory_order_relaxed);
    wnd.enabled.store(settings::enabled, std::memory_order_relaxed);
    wnd.unlockEnabled.store(settings::mountUnlockEnabled, std::memory_order_relaxed);
    wnd.pveEnabled.store(settings::enablePve, std::memory_order_relaxed);
}

std::atomic_bool mountRequested = false;
std::atomic_bool mountPressSent = false;
std::mutex stateMutex;
std::chrono::steady_clock::time_point mountRequestedAt {};
std::chrono::steady_clock::time_point lastProgrammaticPressAt {};
WPARAM lastProgrammaticKey = 0;
std::vector<std::uint32_t> lastTrackedIds;
bool trackedIdsInitialized = false;
bool decisionFeatureWasActive = false;
// Last map mode seen by tick() (true = competitive); only a real change resets
// the decision state, so the anti-spam throttle survives a competitive session
// instead of being wiped every tick.
std::optional<bool> lastCompetitive {};

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
    const std::lock_guard lock(stateMutex);
    lastProgrammaticPressAt = std::chrono::steady_clock::now();
    lastProgrammaticKey = static_cast<WPARAM>(wnd.mountKey.load(std::memory_order_relaxed));
    return true;
}

UINT onMountUnlockWndProc(HWND, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    if (companion::isActive()) return 1;
    if (uMsg != WM_KEYDOWN && uMsg != WM_SYSKEYDOWN) return 1;
    if (wParam != static_cast<WPARAM>(wnd.mountKey.load(std::memory_order_relaxed))) return 1;
    if ((lParam & 0x40000000) != 0) return 1;
    auto* api = boundApi.load(std::memory_order_acquire);
    if (api == nullptr) return 1;
    if (!wnd.enabled.load(std::memory_order_relaxed)) return 1;
    if (!wnd.unlockEnabled.load(std::memory_order_relaxed)) return 1;
    if (!wnd.pveEnabled.load(std::memory_order_relaxed)) return 1;
    if (mumble_link::textInputFocused()) return 1;

    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard lock(stateMutex);
        if (lastProgrammaticPressAt != std::chrono::steady_clock::time_point {}
            && now - lastProgrammaticPressAt < echoGuardWindow
            && wParam == lastProgrammaticKey) {
            return 0;
        }
    }

    // In combat GW2 refuses both the novelty toggle and mounting: pass the key
    // through and let the game show its own refusal.
    if (mumble_link::isInCombat()) return 1;
    if (mumble_link::isCompetitive()) return 1;
    if (mumble_link::mountIndex() != 0) return 1;
    if (!isTransformed()) return 1;

    if (!pressGameBind(api, settings::noveltyBind)) return 1;
    mountRequested.store(true, std::memory_order_release);
    mountPressSent.store(false, std::memory_order_release);
    const std::lock_guard lock(stateMutex);
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
    const std::lock_guard lock(stateMutex);
    mountRequestedAt = {};
    lastProgrammaticPressAt = {};
    lastProgrammaticKey = 0;
    lastTrackedIds.clear();
    trackedIdsInitialized = false;
    decisionFeatureWasActive = false;
    lastCompetitive.reset();
}

void updateBindings(void* apiRaw)
{
    auto* api = static_cast<AddonAPI*>(apiRaw);
    refreshWndSettings();
    if (auto* previous = boundApi.exchange(nullptr, std::memory_order_acq_rel);
        previous != nullptr && previous->WndProc.Deregister != nullptr) {
        previous->WndProc.Deregister(onMountUnlockWndProc);
    }
    boundApi.store(api, std::memory_order_release);
    if (api == nullptr) return;
    // The hook only exists while the feature and its mount helper are both on:
    // a disabled tonic must be strictly zero-cost (no callback at all).
    if (!settings::enabled || !settings::mountUnlockEnabled) return;
    if (api->WndProc.Register == nullptr) return;
    api->WndProc.Register(onMountUnlockWndProc);
}

void tick(void* apiRaw, void*)
{
    auto* api = static_cast<AddonAPI*>(apiRaw);
    if (api == nullptr) return;
    refreshWndSettings();
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
    const auto now = std::chrono::steady_clock::now();

    // Watchdog: a mount request may never outlive its window. The block that
    // services it sits below several early-returns (combat, backend not ready)
    // that used to leave it alive — and a pending request bypasses the tick
    // throttle, so it also re-ran every frame.
    {
        bool stale = false;
        {
            const std::lock_guard lock(stateMutex);
            if (mountRequested.load(std::memory_order_acquire)
                && (mountRequestedAt == std::chrono::steady_clock::time_point {}
                    || now - mountRequestedAt > mountAttemptTimeout)) {
                mountRequestedAt = {};
                stale = true;
            }
        }
        if (stale) {
            mountRequested.store(false, std::memory_order_release);
            mountPressSent.store(false, std::memory_order_release);
        }
    }
    if (!live_data::ready()) return;

    const bool hasMountRequest = mountRequested.load(std::memory_order_acquire);
    if (!hasMountRequest) {
        if (lastTick != std::chrono::steady_clock::time_point {}
            && now - lastTick < tickInterval) return;
        lastTick = now;
    } else {
        lastTick = now;
    }

    // Mode gate: competitive = sPvP + WvW, everything else is PvE. The
    // mount-unlock is PvE-only, so a mount request is always dropped in
    // competitive maps.
    const auto gate = logic::evaluateModeGate(mumble_link::isCompetitive(),
        settings::enablePve, settings::enableCompetitive, lastCompetitive);
    if (gate.clearMount) {
        mountRequested.store(false, std::memory_order_release);
        mountPressSent.store(false, std::memory_order_release);
        const std::lock_guard lock(stateMutex);
        mountRequestedAt = {};
    }
    // Reset only on a real mode change (see evaluateModeGate), otherwise the
    // anti-spam throttle is lost and the novelty bind is pressed every tick.
    if (gate.resetDecision) {
        decisionFeatureWasActive = false;
        decisionState = {};
    }
    if (!gate.enabled) {
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
    // GW2 blocks the novelty toggle while in combat: pressing would spam a
    // bind the game refuses. Suspend the re-press until combat ends; when it
    // ends the normal absence grace applies and the tonic is restored.
    if (mumble_link::isInCombat()) {
        decisionFeatureWasActive = false;
        return;
    }
    if (hasMountRequest) {
        std::chrono::steady_clock::time_point requestedAt {};
        {
            const std::lock_guard lock(stateMutex);
            requestedAt = mountRequestedAt;
        }
        if (mounted) {
            mountRequested.store(false, std::memory_order_release);
            return;
        }
        // The watchdog above already abandoned any request past its timeout.
        if (!mountPressSent.load(std::memory_order_acquire)) {
            // Only press the mount bind once the transformation is really gone
            // and the settle time has passed: a lagging snapshot would fire the
            // mount key while still transformed and the game would swallow it.
            if (now - requestedAt >= mountUnequipSettle && !transformed) {
                if (pressGameBind(api, kMountToggleBind)) {
                    mountPressSent.store(true, std::memory_order_release);
                } else {
                    mountRequested.store(false, std::memory_order_release);
                }
            }
        }
        return;
    }

    logic::DecisionParams params;
    params.rePressDelay = std::chrono::milliseconds {settings::rePressDelayMs};
    // Blocked states (death, combat novelty locks...) back off after two
    // swallowed presses instead of retrying every rePressDelay.
    params.swallowedBeforeBackoff = 2;
    params.blockedRetryDelay = std::chrono::milliseconds {8000};

    if (!logic::decideShouldPress(transformed, mounted, now, params, decisionState)) {
        return;
    }

    pressGameBind(api, settings::noveltyBind);
}

}

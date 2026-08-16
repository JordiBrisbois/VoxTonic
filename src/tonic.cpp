#include "tonic.hpp"

#include "Nexus.h"
#include "live_data_api.hpp"
#include "mumble_link.hpp"
#include "settings.hpp"
#include "tonic_ids.hpp"
#include "tonic_logic.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace voxtonic::tonic {
namespace {

constexpr auto tickInterval = std::chrono::milliseconds {100};
// Hold duration for the invoked game bind, mirroring a short keypress.
constexpr int bindHoldMs = 50;
// EGameBinds value of Mount/Dismount (the player's own bind in GW2 options).
constexpr int kMountToggleBind = 152; // EGameBinds_SpumoniToggle
// Wait this long after the tonic disappears before pressing the mount bind,
// letting GW2 finish the unequip animation.
constexpr auto mountUnequipSettle = std::chrono::milliseconds {250};
// If the mount has not been taken within this window, abandon the unlock and
// let the normal tonic re-press restore the tonic.
constexpr auto mountAttemptTimeout = std::chrono::milliseconds {1500};
// Ignore WndProc messages this soon after our own synthetic press (echo guard).
constexpr auto echoGuardWindow = std::chrono::milliseconds {300};

logic::DecisionState decisionState;
std::chrono::steady_clock::time_point lastTick {};
AddonAPI* boundApi = nullptr;

// Set when the player presses the mount key while transformed on foot in PvE:
// the tick loop then presses the GW2 mount bind once the tonic is gone.
bool mountRequested = false;
bool mountPressSent = false;
std::chrono::steady_clock::time_point mountRequestedAt {};
std::chrono::steady_clock::time_point lastProgrammaticPressAt {};

// True when any configured transformation effect is currently active.
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

void pressGameBind(AddonAPI* api, const int bind)
{
    if (api == nullptr || api->GameBinds.InvokeAsync == nullptr) return;
    api->GameBinds.InvokeAsync(static_cast<EGameBinds>(bind), bindHoldMs);
    lastProgrammaticPressAt = std::chrono::steady_clock::now();
}

// Observes the mount key. The key is passed through to the game untouched in
// every case except one: transformed on foot in PvE, where we block it,
// unequip the tonic and let the tick re-press the GW2 mount bind once the
// effect is gone. Because everything else passes through, dismounting, WvW
// mounting and mounting without a tonic keep working exactly as in vanilla GW2.
UINT onMountUnlockWndProc(HWND, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    if (uMsg != WM_KEYDOWN && uMsg != WM_SYSKEYDOWN) return 1;
    if (wParam != static_cast<WPARAM>(settings::mountUnlockKey)) return 1;
    // Key repeat (held key): let it through.
    if ((lParam & 0x40000000) != 0) return 1;
    if (boundApi == nullptr) return 1;
    if (!settings::enabled) return 1;
    if (!settings::mountUnlockEnabled) return 1;
    // The mount-unlock is a PvE-only helper: when the PvE toggle is off, never
    // intercept the key.
    if (!settings::enablePve) return 1;

    // Swallow the echo of our own synthetic press so it cannot loop.
    const auto now = std::chrono::steady_clock::now();
    if (lastProgrammaticPressAt != std::chrono::steady_clock::time_point {}
        && now - lastProgrammaticPressAt < echoGuardWindow) {
        return 0;
    }

    // Pass-through cases: competitive maps (game handles the tonic), mounted
    // (game handles dismount), not transformed (game mounts normally).
    if (mumble_link::isCompetitive()) return 1;
    if (mumble_link::mountIndex() != 0) return 1;
    if (!isTransformed()) return 1;

    // Transformed on foot in PvE: unequip the tonic, block the key, and let
    // the tick press the GW2 mount bind once the effect is gone.
    mountRequested = true;
    mountPressSent = false;
    mountRequestedAt = now;
    pressGameBind(boundApi, settings::noveltyBind);
    return 0;
}

} // namespace

void reset()
{
    decisionState = {};
    lastTick = {};
    mountRequested = false;
    mountPressSent = false;
    mountRequestedAt = {};
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
    if (!settings::enabled) return;
    if (!live_data::ready()) return;

    const auto now = std::chrono::steady_clock::now();
    if (lastTick != std::chrono::steady_clock::time_point {}
        && now - lastTick < tickInterval) {
        return;
    }
    lastTick = now;

    // Mode gate. "Competitive" covers sPvP and WvW (isCompetitive()); "PvE"
    // covers everything else. When the current mode's toggle is off, return
    // before any snapshot work.
    if (mumble_link::isCompetitive()) {
        if (!settings::enableCompetitive) return;
    } else {
        if (!settings::enablePve) return;
    }

    const bool transformed = isTransformed();
    const bool mounted = mumble_link::mountIndex() != 0;

    // Mount unlock: the player asked to mount while transformed. Once the
    // tonic is unequipped, press the GW2 mount bind once. Stay in this block
    // until the mount is detected, the timeout abandons, or the player mounted
    // through other means. While active the tonic re-press is suppressed so it
    // cannot re-equip before the mount goes through.
    if (mountRequested) {
        if (mounted) {
            mountRequested = false;
            return;
        }
        if (now - mountRequestedAt > mountAttemptTimeout) {
            mountRequested = false;
        } else if (!transformed && now - mountRequestedAt >= mountUnequipSettle
            && !mountPressSent) {
            mountPressSent = true;
            pressGameBind(api, kMountToggleBind);
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

} // namespace voxtonic::tonic

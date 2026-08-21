#pragma once

#include <chrono>

namespace voxtonic::logic {

// Pure decision logic for the tonic auto re-press feature. No Windows headers,
// no Nexus dependency: portable and unit-testable.
//
// The bind pressed is the player's "Equip/Unequip Novelty" (a toggle). The
// logic presses only while the transformation effect is absent and the player
// is on foot (mounted players cannot be transformed, so a mounted check is
// redundant). On startup, an initial grace period gives an already-transformed
// player time to be detected (so the tonic is not toggled off), then the tonic
// is pressed if it is still absent (so the feature transforms the player
// automatically when the addon loads active).

struct DecisionParams {
    // Minimum time between two presses (anti-spam while the tonic is on
    // cooldown or the effect ID is wrong).
    std::chrono::milliseconds rePressDelay {2000};
    // Minimum time the effect must have been absent before pressing. Absorbs
    // the few frames where the buff list lags behind a mount dismount.
    std::chrono::milliseconds absenceGrace {300};
    // Wait this long after the first evaluation before the first press when
    // the effect was never seen active. Covers an incomplete first snapshot
    // right after addon load: pressing during that window could toggle the
    // tonic off while the player is actually transformed. After it elapses
    // with the effect still absent, the tonic is pressed (auto-transform at
    // load when the feature is enabled).
    std::chrono::milliseconds startupDelay {2000};
    // A press whose transformation never appeared (GW2 blocks item use during
    // the dismount hop, eaten binds...) retries after this long.
    std::chrono::milliseconds confirmTimeout {1200};
};

struct DecisionState {
    bool started = false;
    bool everSeenActive = false;
    std::chrono::steady_clock::time_point startedAt {};
    std::chrono::steady_clock::time_point lastActiveAt {};
    std::chrono::steady_clock::time_point lastPressAt {};
    bool pendingConfirm = false;
    std::chrono::steady_clock::time_point pendingSince {};
    bool wasMounted = false;
};

inline bool decideShouldPress(const bool transformed, const bool mounted,
    const std::chrono::steady_clock::time_point now,
    const DecisionParams& params, DecisionState& state)
{
    if (!state.started) {
        state.started = true;
        state.startedAt = now;
    }
    if (transformed) {
        state.everSeenActive = true;
        state.lastActiveAt = now;
        state.pendingConfirm = false;
        state.wasMounted = mounted;
        return false;
    }
    if (!state.everSeenActive && now - state.startedAt < params.startupDelay) {
        return false;
    }
    if (mounted) {
        state.wasMounted = true;
        return false;
    }
    if (state.wasMounted) {
        // Dismounting ends with a short airborne hop during which GW2 refuses
        // item use: arm lastPressAt so the first activation attempt fires
        // rePressDelay after landing instead of instantly into the jump.
        state.wasMounted = false;
        state.lastActiveAt = now - params.absenceGrace;
        state.lastPressAt = now;
        state.pendingConfirm = false;
    }
    if (now - state.lastActiveAt < params.absenceGrace) return false;
    // Novelty is a toggle. While the first press is waiting for confirmation,
    // never send a second one — unless it clearly was swallowed (no
    // transformation after confirmTimeout): then expire the wait and let the
    // lastPressAt throttle schedule a retry.
    if (state.pendingConfirm) {
        if (now - state.pendingSince < params.confirmTimeout) return false;
        state.pendingConfirm = false;
    }
    if (state.lastPressAt != std::chrono::steady_clock::time_point {}
        && now - state.lastPressAt < params.rePressDelay) {
        return false;
    }
    state.lastPressAt = now;
    state.pendingConfirm = true;
    state.pendingSince = now;
    return true;
}

} // namespace voxtonic::logic

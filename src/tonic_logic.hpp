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
    // Re-press spacing while the effect has never been seen active. Right
    // after an addon reload the memory backend can take a moment to publish a
    // stable snapshot; pressing again with the short rePressDelay could toggle
    // the tonic off before GW2 records the activation. This longer delay keeps
    // retrying until the effect is confirmed active.
    std::chrono::milliseconds startupRetryDelay {8000};
};

struct DecisionState {
    bool started = false;
    bool everSeenActive = false;
    std::chrono::steady_clock::time_point startedAt {};
    std::chrono::steady_clock::time_point lastActiveAt {};
    std::chrono::steady_clock::time_point lastPressAt {};
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
        return false;
    }
    if (!state.everSeenActive && now - state.startedAt < params.startupDelay) {
        return false;
    }
    // Never press while mounted: a mounted player cannot be transformed, so
    // re-equipping would toggle the tonic on the mount bar.
    if (mounted) return false;
    if (now - state.lastActiveAt < params.absenceGrace) return false;
    // Until the effect has been seen active once, space re-presses widely so
    // the first press has time to be confirmed by the snapshot before another
    // press could toggle the tonic off.
    const auto retryDelay = state.everSeenActive
        ? params.rePressDelay
        : params.startupRetryDelay;
    // lastPressAt == time_point{} means "never pressed": no anti-spam block.
    if (state.lastPressAt != std::chrono::steady_clock::time_point {}
        && now - state.lastPressAt < retryDelay) return false;
    state.lastPressAt = now;
    return true;
}

} // namespace voxtonic::logic

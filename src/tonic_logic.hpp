#pragma once

#include <chrono>

namespace voxtonic::logic {

struct DecisionParams {
    std::chrono::milliseconds rePressDelay {2000};
    std::chrono::milliseconds absenceGrace {300};
    std::chrono::milliseconds startupDelay {2000};
    // A press whose transformation never appeared (GW2 blocks item use during
    // the dismount hop, eaten binds, death, combat novelty locks...) retries
    // after this long — until repeated failures trigger the back-off below.
    std::chrono::milliseconds confirmTimeout {1200};
    // After this many swallowed presses in a row (transformation never came
    // back), the next press waits for blockedRetryDelay instead of
    // rePressDelay. Covers states with no dedicated detection: dead, combat
    // locks, blocked item usage.
    int swallowedBeforeBackoff = 2;
    std::chrono::milliseconds blockedRetryDelay {8000};
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
    int swallowedPresses = 0;
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
        // A press that produced the transformation counts as delivered: the
        // back-off only exists for swallowed presses.
        state.swallowedPresses = 0;
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
        // item use: arm lastPressAt so the first attempt fires rePressDelay
        // after landing instead of instantly into the jump.
        state.wasMounted = false;
        state.lastActiveAt = now - params.absenceGrace;
        state.lastPressAt = now;
        state.pendingConfirm = false;
    }
    if (now - state.lastActiveAt < params.absenceGrace) return false;
    if (state.pendingConfirm) {
        // Waiting for the transformation to appear. If it never does, the
        // press was swallowed (combat novelty lock, death, blocked item
        // use...): count the failure and back off progressively instead of
        // hammering a bind the game refuses.
        if (now - state.pendingSince < params.confirmTimeout) return false;
        state.pendingConfirm = false;
        ++state.swallowedPresses;
    }
    const auto retryDelay =
        state.swallowedPresses >= params.swallowedBeforeBackoff
            ? params.blockedRetryDelay
            : params.rePressDelay;
    if (state.lastPressAt != std::chrono::steady_clock::time_point {}
        && now - state.lastPressAt < retryDelay) {
        return false;
    }
    state.lastPressAt = now;
    state.pendingConfirm = true;
    state.pendingSince = now;
    return true;
}

}

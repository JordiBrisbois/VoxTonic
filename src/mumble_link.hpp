#pragma once

#include <cstdint>

namespace voxtonic::mumble_link {

/// One-shot view of every MumbleLink signal the addon needs. Callers on a hot
/// path read this once instead of taking the cache lock once per accessor.
struct Signals {
    bool competitive = false;
    bool inCombat = false;
    bool textInput = false;
    int mount = 0;
    // Raw context, exposed so the diagnostics panel can show what the mode
    // checks actually read. The uiState bit assignment was validated in game
    // (chat = bit 5, combat = bit 6); competitive is a map-type classification.
    std::uint32_t mapId = 0;
    std::uint32_t mapType = 0;
    std::uint32_t uiState = 0;
};

Signals readSignals();

bool isCompetitive();
int mountIndex();
bool textInputFocused();
bool isInCombat();

} // namespace voxtonic::mumble_link

#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

// Public API of the VoxTonic live-data backend (private VoxTonic-RE repo). It
// lives here rather than in the backend repo so the public addon can build
// against the stub (VOXT_ENABLE_LIVE_DATA=OFF) without the private checkout —
// the same split VoxSake uses.
//
// Self-only by design: reads the local player's buff bar and reports which
// tracked transformation effects are currently active. No roster, no squad, no
// remote API.
namespace voxtonic::live_data {

struct EffectState {
    std::uint32_t id = 0;
    int stacks = 0;
    std::chrono::milliseconds remaining {0};
    bool indefinite = false;
};

struct Snapshot {
    std::vector<EffectState> effects;
    std::chrono::steady_clock::time_point capturedAt {};
    std::uint64_t generation = 0;
};

struct Diagnostics {
    int scanMicroseconds = 0;
    int snapshotAgeMilliseconds = -1;
    std::uint64_t cacheRefreshes = 0;
    std::uint64_t unavailableReads = 0;
};

// Outcome of the last shutdown(), for diagnostics. The game-thread hook can only
// be torn down once it is provably stopped; when it is not, the backend has to
// pin its own module, which keeps the DLL locked for the rest of the process and
// makes a Nexus hot-reload impossible. Reporting the two cases is the only way
// to tell them apart from the outside.
struct ShutdownReport {
    int disableStatus = 0;          // MinHook status of the last disable attempt (0 = MH_OK)
    int disableAttempts = 0;
    int inFlightBeforeDisable = 0;  // detours still running when shutdown started
    int inFlightAfterDisable = 0;   // last reading during the confirmation window
    bool hookStopped = false;       // the hook can no longer call into the addon
    bool drained = false;           // no detour observed: everything was freed
    bool pinned = false;            // module pinned, so the DLL stays locked until GW2 exits
};

ShutdownReport lastShutdownReport();

bool initialize(void* hostApi);   // AddonAPI* from Nexus
void shutdown();
void pump();                      // no-op: collection happens on the game thread
bool ready();                     // a fresh snapshot exists (<= 5 s old)
void setTrackedIds(std::vector<std::uint32_t> ids);
std::vector<std::uint32_t> activeEffectIds();
Snapshot snapshot();
Diagnostics diagnostics();
const char* diagnosticStage();
// Human-readable detail of the current stage (e.g. why the buff bar could not
// be resolved). Never null.
const char* diagnosticDetail();

} // namespace voxtonic::live_data

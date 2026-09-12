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
#include <vector>

namespace voxtonic::tonic {
namespace {

constexpr auto tickInterval = std::chrono::milliseconds {100};
constexpr int bindHoldMs = 50;
constexpr int kMountToggleBind = 152; // EGameBinds_SpumoniToggle
constexpr auto mountUnequipSettle = std::chrono::milliseconds {250};
constexpr auto mountAttemptTimeout = std::chrono::milliseconds {1500};
constexpr auto echoGuardWindow = std::chrono::milliseconds {180};
// If the mount press is eaten by the game (unequip still finishing), retry the
// bind at this interval until the attempt timeout.
constexpr auto mountRetryInterval = std::chrono::milliseconds {400};
// The unequip press is retried on this (longer) interval while the tonic is
// still on: long enough that a successful unequip has had time to show up in
// MumbleLink, so the retry cannot toggle the tonic back on by mistake.
constexpr auto mountUnequipRetryInterval = std::chrono::milliseconds {500};

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
    std::atomic<int> gameBind {0};
    std::atomic<int> mountProbeStage {0};
    std::atomic<int> tickMountStage {0};
    // MumbleLink context as the gates read it on the last mount-key press, so
    // the options panel can explain exactly why a press did what it did.
    std::atomic<int> probeMapId {0};
    std::atomic<int> probeMapType {0};
    std::atomic<unsigned> probeUiState {0};
    std::atomic<int> probeCompetitive {-1};
    std::atomic<int> probeInCombat {-1};
    std::atomic<int> probeMount {-1};
    // Diagnostic: outcome of the last updateBindings() registration, and the
    // last virtual-key code any WM_KEYDOWN carried. Together they tell "hook
    // never installed" apart from "wrong key" without a debugger.
    std::atomic<int> hookState {0};
    std::atomic<int> lastVk {0};
};
WndSettings wnd;

// WndProc registration outcome. Nexus' Register/Deregister return void, so the
// addon records what it did itself instead of a return value.
enum class HookState : int {
    NotCalled = 0,
    Registered = 1,
    ApiMissing = 2,
    Disabled = 3,
};

// Mount-unlock key capture handshake. The window thread owns Armed -> Captured;
// the render thread owns Captured -> Off (see consumeCapturedMountKey). The
// separate Captured state matters because refreshWndSettings() runs every tick
// and must not re-arm a capture that already happened.
enum class KeyCapture : int {
    Off = 0,
    Armed = 1,
    Captured = 2,
};
std::atomic<int> keyCaptureState {0};
std::atomic<int> capturedKey {0};
// Steady-clock milliseconds of the last options-panel frame; the capture is
// cancelled when it goes stale.
std::atomic<std::int64_t> lastOptionsUiRenderMs {0};

std::int64_t steadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Diagnostic stages surfaced in the options panel, so a stuck mount-unlock can
// be diagnosed from the UI without a debugger. The integer values are stable.
enum class ProbeStage : int {
    KeyDown = 1,
    KeyRepeat = 2,
    NoApi = 3,
    FeatureOff = 4,
    Matched = 5,
    UnlockOff = 7,
    EchoGuard = 8,
    Ready = 9,
    Competitive = 10,
    Mounted = 11,
    NotTransformed = 14,
    PressFailed = 15,
    Triggered = 16,
    PveOff = 17,
    TextInput = 18,
    InCombat = 19,
};

enum class TickMountStage : int {
    Mounted = 2,
    Timeout = 3,
    Pressed = 4,
    PressFailed = 5,
    ReUnequipped = 7,
};

void setProbeStage(const ProbeStage stage)
{
    wnd.mountProbeStage.store(static_cast<int>(stage), std::memory_order_relaxed);
}

void setTickMountStage(const TickMountStage stage)
{
    wnd.tickMountStage.store(static_cast<int>(stage), std::memory_order_relaxed);
}

void refreshWndSettings()
{
    wnd.mountKey.store(settings::mountUnlockKey, std::memory_order_relaxed);
    wnd.enabled.store(settings::enabled, std::memory_order_relaxed);
    wnd.unlockEnabled.store(settings::mountUnlockEnabled, std::memory_order_relaxed);
    wnd.pveEnabled.store(settings::enablePve, std::memory_order_relaxed);
    wnd.gameBind.store(settings::noveltyBind, std::memory_order_relaxed);
    // A capture only makes sense while the options panel is open: cancel it once
    // the panel stops rendering so a later gameplay keypress is not swallowed
    // and silently bound as the mount key.
    if (settings::mountUnlockKeyCapture) {
        const auto lastRender = lastOptionsUiRenderMs.load(std::memory_order_relaxed);
        if (lastRender == 0 || steadyNowMs() - lastRender > 250) {
            settings::mountUnlockKeyCapture = false;
        }
    }
    // Arm the capture only from Off: a capture the window thread already
    // completed (Captured) must survive this per-tick republish.
    if (settings::mountUnlockKeyCapture) {
        int expected = static_cast<int>(KeyCapture::Off);
        keyCaptureState.compare_exchange_strong(expected, static_cast<int>(KeyCapture::Armed),
            std::memory_order_relaxed, std::memory_order_relaxed);
    } else {
        keyCaptureState.store(static_cast<int>(KeyCapture::Off), std::memory_order_relaxed);
    }
}

std::atomic_bool mountRequested = false;
std::atomic_bool mountPressSent = false;
// The unequip press can be eaten by the game (the novelty toggle is refused at
// some instants). It is retried while the tonic is still on, otherwise the
// mount presses never become safe and the attempt times out with the player
// still transformed.
bool unequipPressSent = false;
int g_mountPressCount = 0; // bind presses issued for the current request
std::mutex stateMutex;
std::chrono::steady_clock::time_point mountRequestedAt {};
std::chrono::steady_clock::time_point lastMountPressAt {};
std::chrono::steady_clock::time_point lastUnequipPressAt {};
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
    lastProgrammaticKey = static_cast<WPARAM>(bind == kMountToggleBind
        ? wnd.mountKey.load(std::memory_order_relaxed)
        : wnd.gameBind.load(std::memory_order_relaxed));
    return true;
}

UINT onMountUnlockWndProc(HWND, const UINT uMsg, const WPARAM wParam, const LPARAM lParam)
{
    if (companion::isActive()) return 1;
    const bool keyDown = uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN;
    if (!keyDown) return 1;
    // Record every key the game sends while the hook is installed: if the mount
    // key never works, this is what tells "hook not installed" (lastVk stays 0)
    // apart from "wrong key" (lastVk shows what the game actually received).
    wnd.lastVk.store(static_cast<int>(wParam), std::memory_order_relaxed);

    // Key-capture mode: swallow the next press and hand its VK to the render
    // thread, which persists it and re-registers the hook.
    if (keyCaptureState.load(std::memory_order_relaxed) == static_cast<int>(KeyCapture::Armed)) {
        capturedKey.store(static_cast<int>(wParam), std::memory_order_relaxed);
        keyCaptureState.store(static_cast<int>(KeyCapture::Captured), std::memory_order_relaxed);
        return 0;
    }

    if (wParam != static_cast<WPARAM>(wnd.mountKey.load(std::memory_order_relaxed))) return 1;

    setProbeStage(ProbeStage::KeyDown);
    // Key repeat (held key): let it through.
    if ((lParam & 0x40000000) != 0) { setProbeStage(ProbeStage::KeyRepeat); return 1; }
    auto* api = boundApi.load(std::memory_order_acquire);
    if (api == nullptr) { setProbeStage(ProbeStage::NoApi); return 1; }
    if (!wnd.enabled.load(std::memory_order_relaxed)) { setProbeStage(ProbeStage::FeatureOff); return 1; }
    setProbeStage(ProbeStage::Matched);

    // Sample MumbleLink once and record it: every gate below reads the same
    // sample, and the options panel can show exactly what was seen.
    const auto signals = mumble_link::readSignals();
    wnd.probeMapId.store(static_cast<int>(signals.mapId), std::memory_order_relaxed);
    wnd.probeMapType.store(static_cast<int>(signals.mapType), std::memory_order_relaxed);
    wnd.probeUiState.store(signals.uiState, std::memory_order_relaxed);
    wnd.probeCompetitive.store(signals.competitive ? 1 : 0, std::memory_order_relaxed);
    wnd.probeInCombat.store(signals.inCombat ? 1 : 0, std::memory_order_relaxed);
    wnd.probeMount.store(signals.mount, std::memory_order_relaxed);

    if (!wnd.unlockEnabled.load(std::memory_order_relaxed)) { setProbeStage(ProbeStage::UnlockOff); return 1; }
    if (signals.textInput) { setProbeStage(ProbeStage::TextInput); return 1; }
    // The mount-unlock is a PvE-only helper: when the PvE toggle is off, never
    // intercept the key.
    if (!wnd.pveEnabled.load(std::memory_order_relaxed)) { setProbeStage(ProbeStage::PveOff); return 1; }

    // Swallow the echo of our own synthetic press so it cannot loop.
    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard lock(stateMutex);
        if (lastProgrammaticPressAt != std::chrono::steady_clock::time_point {}
            && now - lastProgrammaticPressAt < echoGuardWindow
            && wParam == lastProgrammaticKey) {
            setProbeStage(ProbeStage::EchoGuard);
            return 0;
        }
    }
    setProbeStage(ProbeStage::Ready);

    // Pass-through cases: combat (GW2 refuses both the novelty toggle and the
    // mount), competitive maps (the game handles the tonic), mounted (the game
    // handles dismount), not transformed (the game mounts normally).
    if (signals.inCombat) { setProbeStage(ProbeStage::InCombat); return 1; }
    if (signals.competitive) { setProbeStage(ProbeStage::Competitive); return 1; }
    if (signals.mount != 0) { setProbeStage(ProbeStage::Mounted); return 1; }
    if (!isTransformed()) { setProbeStage(ProbeStage::NotTransformed); return 1; }

    // Transformed on foot in PvE: unequip the tonic, block the key, and let the
    // tick press the GW2 mount bind once the effect is gone.
    if (!pressGameBind(api, wnd.gameBind.load(std::memory_order_relaxed))) {
        setProbeStage(ProbeStage::PressFailed);
        return 1;
    }
    {
        const std::lock_guard lock(stateMutex);
        unequipPressSent = true;
        g_mountPressCount = 0;
        lastMountPressAt = {};
        lastUnequipPressAt = now;
        mountRequestedAt = now;
    }
    mountRequested.store(true, std::memory_order_release);
    mountPressSent.store(false, std::memory_order_release);
    setProbeStage(ProbeStage::Triggered);
    return 0;
}

} // namespace

void reset()
{
    decisionState = {};
    lastTick = {};
    mountRequested.store(false, std::memory_order_release);
    mountPressSent.store(false, std::memory_order_release);
    const std::lock_guard lock(stateMutex);
    unequipPressSent = false;
    g_mountPressCount = 0;
    mountRequestedAt = {};
    lastMountPressAt = {};
    lastUnequipPressAt = {};
    lastProgrammaticPressAt = {};
    lastProgrammaticKey = 0;
    lastTrackedIds.clear();
    trackedIdsInitialized = false;
    decisionFeatureWasActive = false;
    lastCompetitive.reset();
    keyCaptureState.store(static_cast<int>(KeyCapture::Off), std::memory_order_relaxed);
    capturedKey.store(0, std::memory_order_relaxed);
    wnd.lastVk.store(0, std::memory_order_relaxed);
    wnd.hookState.store(static_cast<int>(HookState::NotCalled), std::memory_order_relaxed);
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
    if (api == nullptr) {
        wnd.hookState.store(static_cast<int>(HookState::NotCalled), std::memory_order_relaxed);
        return;
    }
    // The hook exists while the feature and its mount helper are both on, and
    // while the options panel is capturing a key; otherwise the addon installs
    // no callback at all (a disabled tonic stays strictly zero-cost).
    if (!settings::mountUnlockKeyCapture
        && (!settings::enabled || !settings::mountUnlockEnabled)) {
        wnd.hookState.store(static_cast<int>(HookState::Disabled), std::memory_order_relaxed);
        return;
    }
    if (api->WndProc.Register == nullptr) {
        wnd.hookState.store(static_cast<int>(HookState::ApiMissing), std::memory_order_relaxed);
        return;
    }
    api->WndProc.Register(onMountUnlockWndProc);
    wnd.hookState.store(static_cast<int>(HookState::Registered), std::memory_order_relaxed);
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
            const std::lock_guard lock(stateMutex);
            setTickMountStage(TickMountStage::Timeout);
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
        // Decided under the lock, executed outside it: pressGameBind() itself
        // takes stateMutex and std::mutex is not recursive.
        bool sendMountPress = false;
        bool sendUnequipPress = false;
        {
            const std::lock_guard lock(stateMutex);
            if (mounted) {
                mountRequested.store(false, std::memory_order_release);
                setTickMountStage(TickMountStage::Mounted);
                return;
            }
            if (transformed && now - mountRequestedAt >= mountUnequipRetryInterval
                && (!unequipPressSent
                    || now - lastUnequipPressAt >= mountUnequipRetryInterval)) {
                // The unequip press sent by the WndProc was eaten: the tonic is
                // still on after the retry interval, so the mount press would
                // never become safe. Re-send it.
                sendUnequipPress = true;
            } else if (!transformed && now - mountRequestedAt >= mountUnequipSettle
                && (!mountPressSent.load(std::memory_order_acquire)
                    || now - lastMountPressAt >= mountRetryInterval)) {
                // Only press the mount bind once the transformation is really
                // gone and the settle time has passed: a lagging snapshot would
                // fire the mount key while still transformed and the game would
                // swallow it. The first press can also be eaten, so it retries.
                sendMountPress = true;
            }
        }
        if (sendUnequipPress) {
            if (pressGameBind(api, wnd.gameBind.load(std::memory_order_relaxed))) {
                const std::lock_guard lock(stateMutex);
                unequipPressSent = true;
                lastUnequipPressAt = now;
                setTickMountStage(TickMountStage::ReUnequipped);
            }
        } else if (sendMountPress) {
            if (pressGameBind(api, kMountToggleBind)) {
                const std::lock_guard lock(stateMutex);
                mountPressSent.store(true, std::memory_order_release);
                g_mountPressCount += 1;
                lastMountPressAt = now;
                setTickMountStage(TickMountStage::Pressed);
            } else {
                mountRequested.store(false, std::memory_order_release);
                setTickMountStage(TickMountStage::PressFailed);
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

    pressGameBind(api, wnd.gameBind.load(std::memory_order_relaxed));
}

int mountProbeStage()
{
    return wnd.mountProbeStage.load(std::memory_order_relaxed);
}

int tickMountStage()
{
    return wnd.tickMountStage.load(std::memory_order_relaxed);
}

ProbeSnapshot lastProbeSignals()
{
    ProbeSnapshot out;
    out.mapId = wnd.probeMapId.load(std::memory_order_relaxed);
    out.mapType = wnd.probeMapType.load(std::memory_order_relaxed);
    out.uiState = wnd.probeUiState.load(std::memory_order_relaxed);
    out.competitive = wnd.probeCompetitive.load(std::memory_order_relaxed);
    out.inCombat = wnd.probeInCombat.load(std::memory_order_relaxed);
    out.mount = wnd.probeMount.load(std::memory_order_relaxed);
    return out;
}

int mountPressCount()
{
    const std::lock_guard lock(stateMutex);
    return g_mountPressCount;
}

int wndProcHookState()
{
    return wnd.hookState.load(std::memory_order_relaxed);
}

int lastSeenVirtualKey()
{
    return wnd.lastVk.load(std::memory_order_relaxed);
}

bool mountKeyCaptureArmed()
{
    return keyCaptureState.load(std::memory_order_relaxed) == static_cast<int>(KeyCapture::Armed);
}

void notifyOptionsUiRendered()
{
    lastOptionsUiRenderMs.store(steadyNowMs(), std::memory_order_relaxed);
}

bool consumeCapturedMountKey(int& vk)
{
    if (keyCaptureState.load(std::memory_order_relaxed)
        != static_cast<int>(KeyCapture::Captured)) {
        return false;
    }
    vk = capturedKey.load(std::memory_order_relaxed);
    keyCaptureState.store(static_cast<int>(KeyCapture::Off), std::memory_order_relaxed);
    return true;
}

} // namespace voxtonic::tonic

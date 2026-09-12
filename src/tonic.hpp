#pragma once

namespace voxtonic::tonic {

// Resets the auto-press decision state (on addon load/unload and when the
// feature is disabled).
void reset();

// Runs the tonic auto re-press loop. Called from the render callback every
// frame (throttled internally), independent of whether any overlay is shown.
void tick(void* api, void* liveData);

// (Re)registers or deregisters the mount-unlock WndProc depending on the
// current settings. Called at load, unload and whenever the settings change.
void updateBindings(void* api);

/** Diagnostic: last WndProc gate reached by a mount-key press.
 *  16 = mount unlock triggered; anything else = pass-through/block reason
 *  (19 = in combat, 18 = chat focused, 10 = competitive, 11 = mounted). */
int mountProbeStage();

/** Diagnostic: last tick-level mount handling stage (2=mounted, 3=timeout
 *  abandoned, 4=mount bind pressed, 5=press failed, 7=unequip retried). */
int tickMountStage();

/// Raw MumbleLink context as the mount-key gates read it on the last press.
/// Exposed so the options panel can explain a press without a debugger.
struct ProbeSnapshot {
    int mapId = 0;
    int mapType = 0;
    unsigned uiState = 0;
    int competitive = -1;
    int inCombat = -1;
    int mount = -1;
};

ProbeSnapshot lastProbeSignals();

int mountPressCount();

/** Diagnostic: outcome of the last WndProc registration.
 *  0 = hook never registered, 1 = registered, 2 = WndProc.Register unavailable,
 *  3 = disabled (tonic off and no capture in progress). */
int wndProcHookState();

/** Diagnostic: last virtual-key code the WndProc saw (0 = nothing yet). */
int lastSeenVirtualKey();

/// True while the WndProc is armed to swallow the next key press.
bool mountKeyCaptureArmed();

/** Called by the options UI every frame it renders, so the capture can be
 *  cancelled when the panel stops being shown. */
void notifyOptionsUiRendered();

/// If a key was captured since the last call, writes its virtual-key code into
/// `vk`, clears the capture and returns true (returns true only once).
bool consumeCapturedMountKey(int& vk);

} // namespace voxtonic::tonic

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

} // namespace voxtonic::tonic

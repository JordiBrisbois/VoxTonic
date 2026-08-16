#pragma once

#include <cstdint>
#include <string>

namespace voxtonic::settings {

extern bool enabled;
extern std::uint32_t effectId;       // precise id when scanAll is off
extern bool scanAll;                 // scan all known transformation ids
extern bool enablePve;               // mode gate: PvE
extern bool enableCompetitive;       // mode gate: sPvP + WvW
extern int rePressDelayMs;           // anti-spam between presses
extern bool mountUnlockEnabled;      // PvE-only: unequip tonic on mount press
extern int mountUnlockKey;           // VK code of the mount key (88 = X)
extern int noveltyBind;              // EGameBinds value of Equip/Unequip Novelty (162)

void load();
void saveIfChanged(bool force = false);
bool changed();

} // namespace voxtonic::settings

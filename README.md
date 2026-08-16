# VoxTonic

Guild Wars 2 Nexus addon that keeps your cosmetic tonic transformation active.

While enabled, VoxTonic watches the local player's buff bar (via the private
`VoxTonic-RE` direct-memory backend) and presses the GW2 **Equip/Unequip
Novelty** bind whenever the transformation effect drops — so you stay
transformed automatically. PvE and competitive (sPvP + WvW) modes can be
toggled independently; the PvE-only mount helper unequips the tonic when you
press your mount key so the mount can go through, and the tonic comes back
after dismounting.

No license, no overlay: the options window lives in the Nexus addon list.

## Build

```bash
cmake -S VoxTonic -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DVOXT_ENABLE_LIVE_DATA=ON -DVOXT_LIVE_DATA_DIR=../VoxTonic-RE \
  -DVOXT_INSTALL_DIR="<game>/addons"
cmake --build build
```

Requires MinGW-w64 and Ninja. The CI workflow builds the release DLL the same
way and publishes a GitHub release with a timestamp version (`vYYYY.M.D.HHMM`).

## Settings

Stored in `<game>/addons/VoxTonic/settings.ini`:

- `enabled` — master switch
- `effect_id` — precise transformation id when `scan_all` is off
- `scan_all` — detect among all known transformation ids
- `enable_pve` / `enable_competitive` — mode gates (sPvP + WvW are competitive)
- `repress_delay_ms` — anti-spam between presses
- `mount_unlock_enabled` / `mount_unlock_key` — PvE mount helper (VK code)
- `novelty_bind` — EGameBinds id of Equip/Unequip Novelty (default 162)

## Dependencies

- Nexus (required)
- VoxTonic-RE (private direct-memory backend)

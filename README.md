# VoxTonic

VoxTonic keeps your cosmetic tonic transformation active in Guild Wars 2. When
the transformation wears off, it automatically presses the game's
**Equip/Unequip Novelty** bind to bring it right back — so your tonic stays on
without you having to think about it.

No overlay, no license: the options window lives in the Nexus addon list.

## Features

- **Auto re-press** — re-activates your equipped tonic whenever the
  transformation effect drops (works while on foot).
- **Mount helper (PvE)** — when you press your mount key while transformed,
  the tonic is unequipped first so the mount can go through; it comes back
  automatically after you dismount. Dismounting is never automated.
- **Mode gates** — enable the re-press in PvE and/or competitive (sPvP + WvW)
  independently. With neither checked the addon is fully inert.
- **Known tonic ids** — an optional "scan all known ids" mode auto-detects any
  transformation from the list of well-known cosmetic tonics, so you do not
  need to look up an id. You can also configure a single effect id.

## Requirements

- Guild Wars 2
- [Nexus](https://github.com/RaidcoreGG/Nexus) (addon loader)

## Install

1. Download the latest `VoxTonic.dll` from the
   [Releases](https://github.com/JordiBrisbois/VoxTonic/releases) page.
2. Place it in `<game>/addons/` (next to `Nexus/`).
3. Reload the addon list in Nexus (or restart the game).
4. Open the Nexus options and enable VoxTonic in the addon list.

## Setup

VoxTonic presses the game's own actions for you, so two things must point at
it before anything happens:

1. **Equip the tonic in-game** — put it in your hero panel's **Novelty slot**
   (the toggle acts on whatever is equipped there, not on an inventory item).
2. **Assign the novelty bind in Nexus** — open the Nexus options, go to
   **Keybinds → Guild Wars 2**, and set a key on **Equip/Unequip Novelty**.
   The addon triggers that exact action; with no key assigned there is nothing
   to press.
3. *(Mount helper only, PvE)* Set **Mount key** in the VoxTonic options to the
   same key your in-game mount summon uses (default `X`). When you press it
   while transformed, the tonic is removed so the mount goes through, then
   re-applied after you dismount. Dismounting is never automated.

## Settings

Stored in `<game>/addons/VoxTonic/settings.ini` (editable from the options
window):

- **Enable** — master switch for the auto re-press.
- **Modes** — PvE and/or Competitive (sPvP + WvW).
- **Effect** — "Scan all known tonic ids" (auto-detect) or a precise
  transformation effect id.
- **Re-press delay** — minimum time between two presses (anti-spam).
- **Mount helper** — PvE-only: unequip the tonic when the mount key is
  pressed. Configure the mount key by its Windows virtual-key code (88 = X).
- **Novelty bind** — the game bind that toggles your tonic (default 162 =
  Equip/Unequip Novelty; leave it unless you know what you are doing).

## Tips

- If the status shows a transformation id you recognize, that tonic is
  currently active and the addon is holding it.
- "Scan all known tonic ids" is the easiest setup: enable it and the addon
  takes care of the rest.
- Some tonics share ids; if a specific tonic is not detected, equip it in-game
  and check the status line — it shows the active transformation id, which you
  can then set manually in the Effect field.

## Disclaimer

VoxTonic reads your character's buff state and presses a game bind you have
already configured yourself. Use at your own risk.

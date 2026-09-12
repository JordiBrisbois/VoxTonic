#include "ui.hpp"

#include "Nexus.h"
#include "imgui.h"
#include "companion.hpp"
#include "live_data_api.hpp"
#include "mumble_link.hpp"
#include "settings.hpp"
#include "tonic.hpp"
#include "tonic_ids.hpp"

#include <cstdio>
#include <string>

namespace voxtonic::ui {
namespace {

AddonAPI* api = nullptr;

// Human-readable name for a virtual-key code, so the mount key is shown as the
// key itself (e.g. "X") instead of a bare number.
std::string mountKeyName(const int vk)
{
    if (vk <= 0) return "(not set)";
    wchar_t name[64] {};
    const auto scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    if (GetKeyNameTextW(static_cast<LONG>(scan) << 16, name, 64) > 0) {
        char narrow[128] {};
        if (WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow),
                nullptr, nullptr) > 0) {
            return narrow;
        }
    }
    return "VK " + std::to_string(vk);
}

const char* hookStateLabel(const int state)
{
    switch (state) {
    case 1: return "registered";
    case 2: return "no WndProc API";
    case 3: return "disabled (tonic off)";
    default: return "not registered";
    }
}

}

void setApi(void* addonApi) { api = static_cast<AddonAPI*>(addonApi); }

void render()
{
}

void renderOptions()
{
    if (companion::isActive()) {
        ImGui::TextColored({0.9f, 0.55f, 0.3f, 1.0f},
            "VoxSake detected — VoxTonic is disabled.");
        ImGui::TextDisabled(
            "The tonic auto re-press is handled by VoxSake. "
            "Unload VoxSake or remove its DLL to re-enable VoxTonic.");
        return;
    }

    // Tell the tonic module the options panel is live, so an armed key capture
    // survives and is only cancelled once the panel stops rendering.
    tonic::notifyOptionsUiRendered();

    bool changed = false;

    ImGui::TextUnformatted("VoxTonic — cosmetic tonic auto re-press");
    ImGui::Separator();

    if (ImGui::Checkbox("Enable tonic auto re-press", &settings::enabled)) {
        settings::markChanged();
        // The mount-unlock hook only lives while the feature is on.
        tonic::updateBindings(api);
        changed = true;
    }
    ImGui::TextDisabled(
        "Presses the GW2 \"Equip/Unequip Novelty\" bind when the transformation "
        "effect is no longer active. Works while on foot.");

    ImGui::Separator();
    ImGui::TextUnformatted("Modes:");
    if (ImGui::Checkbox("PvE", &settings::enablePve)) {
        settings::markChanged();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Competitive (sPvP + WvW)", &settings::enableCompetitive)) {
        settings::markChanged();
        changed = true;
    }
    ImGui::TextDisabled(
        "The re-press runs in the modes checked. With neither checked the "
        "feature is fully inert (no performance cost). The mount-unlock below "
        "is a PvE-only helper and only applies while the PvE toggle is on.");

    ImGui::Separator();
    ImGui::TextUnformatted("Effect:");
    if (ImGui::Checkbox("Scan all known tonic ids", &settings::scanAll)) {
        settings::markChanged();
        changed = true;
    }
    if (!settings::scanAll) {
        int effectId = static_cast<int>(settings::effectId);
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputInt("Transformation effect ID", &effectId)) {
            if (effectId > 0 && effectId <= 10'000'000) {
                settings::effectId = static_cast<std::uint32_t>(effectId);
                settings::markChanged();
                changed = true;
            }
        }
        ImGui::TextDisabled(
            "The effect id seen in the buff bar while transformed. Leave 0 and "
            "enable \"Scan all known tonic ids\" to auto-detect.");
    } else {
        ImGui::TextDisabled("%zu known tonic ids are scanned per tick.",
            ids::kKnownTonicIds.size());
    }

    if (live_data::ready()) {
        const auto active = live_data::activeEffectIds();
        if (active.empty()) {
            ImGui::TextColored({0.8f, 0.8f, 0.8f, 1.0f}, "Status: ready, no transformation active.");
        } else {
            char line[128] {};
            std::snprintf(line, sizeof(line), "Status: transformation active (%u)%s",
                active.front(), active.size() > 1 ? " + more" : "");
            ImGui::TextColored({0.5f, 0.9f, 0.5f, 1.0f}, "%s", line);
        }
    } else {
        ImGui::TextColored({0.9f, 0.6f, 0.3f, 1.0f}, "Status: backend not ready (%s)",
            live_data::diagnosticStage());
        ImGui::TextDisabled("Detail: %s", live_data::diagnosticDetail());
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputInt("Re-press delay (ms)", &settings::rePressDelayMs)) {
        settings::markChanged();
        changed = true;
    }
    ImGui::TextDisabled("Minimum time between two presses (anti-spam).");

    ImGui::Separator();
    // The mount helper shares the tonic transformation detection, so it is inert
    // while the feature itself is off. Say so, instead of letting a checked box
    // look like it should work.
    if (!settings::enabled) {
        ImGui::TextColored({1.0f, 0.6f, 0.4f, 1.0f},
            "Inert: tick \"Enable tonic auto re-press\" first. The mount helper "
            "shares the transformation detection, so it does nothing while the "
            "tonic feature is off.");
    }
    bool mountUnlock = settings::mountUnlockEnabled;
    if (ImGui::Checkbox("Unequip tonic on mount press (PvE only)", &mountUnlock)) {
        settings::mountUnlockEnabled = mountUnlock;
        settings::markChanged();
        tonic::updateBindings(api);
        changed = true;
    }
    // Key capture instead of a raw virtual-key field: the player presses the key
    // they actually use to mount.
    int capturedVk = 0;
    if (tonic::consumeCapturedMountKey(capturedVk)) {
        settings::mountUnlockKey = capturedVk;
        settings::mountUnlockKeyCapture = false;
        settings::markChanged();
        tonic::updateBindings(api);
        changed = true;
    }
    if (settings::mountUnlockKeyCapture) {
        if (ImGui::Button("Press your mount key... (click to cancel)")) {
            settings::mountUnlockKeyCapture = false;
            tonic::updateBindings(api);
        }
    } else if (ImGui::Button("Press your mount key...")) {
        settings::mountUnlockKeyCapture = true;
        tonic::updateBindings(api);
    }
    ImGui::SameLine();
    ImGui::Text("Mount key: %s (VK %d)",
        mountKeyName(settings::mountUnlockKey).c_str(), settings::mountUnlockKey);
    ImGui::TextDisabled(
        "Click the button then press your GW2 mount key. While transformed on "
        "foot in PvE, pressing it unequips the tonic and then presses your GW2 "
        "Mount/Dismount bind so the mount goes through. Every other case "
        "(mounted, WvW/SPvP, no tonic) lets the key pass through untouched. "
        "Mount unlock is PvE only and never engages in competitive maps.");

    ImGui::Separator();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("Novelty bind id", &settings::noveltyBind)) {
        settings::markChanged();
        changed = true;
    }
    ImGui::TextDisabled("EGameBinds value of the bind to press. 162 = Equip/Unequip Novelty.");

    // Live MumbleLink context and the mount-press diagnostics: a wrong gate has
    // to be diagnosable from the UI instead of guessable.
    ImGui::Separator();
    const auto signals = mumble_link::readSignals();
    ImGui::TextDisabled("Mumble now: map %u (type %u) · uiState 0x%05X · "
        "competitive=%d combat=%d chat=%d mount=%d",
        signals.mapId, signals.mapType, signals.uiState, signals.competitive ? 1 : 0,
        signals.inCombat ? 1 : 0, signals.textInput ? 1 : 0, signals.mount);
    const auto probe = tonic::lastProbeSignals();
    ImGui::TextDisabled("Mumble at last mount press: map %d (type %d) · uiState 0x%05X "
        "· competitive=%d combat=%d mount=%d",
        probe.mapId, probe.mapType, probe.uiState, probe.competitive, probe.inCombat,
        probe.mount);
    ImGui::TextDisabled("Mount-press probe stage: %d (16 = unlock triggered, "
        "19 = in combat, 18 = chat focused, 10 = competitive, 11 = mounted, "
        "14/15 = not transformed / press failed).",
        tonic::mountProbeStage());
    ImGui::TextDisabled("Tick stage: %d (2=mounted, 3=timeout, 4=mount pressed, "
        "5=press failed, 7=unequip retried) · Mount presses: %d",
        tonic::tickMountStage(), tonic::mountPressCount());
    const int lastVk = tonic::lastSeenVirtualKey();
    ImGui::TextDisabled("WndProc hook: %s · last key seen: %s (VK %d) · "
        "tonic=%d unlock=%d capture=%d",
        hookStateLabel(tonic::wndProcHookState()),
        lastVk != 0 ? mountKeyName(lastVk).c_str() : "(none)", lastVk,
        settings::enabled ? 1 : 0, settings::mountUnlockEnabled ? 1 : 0,
        settings::mountUnlockKeyCapture ? 1 : 0);

    if (changed) {
        settings::saveIfChanged(false);
    }
}

}

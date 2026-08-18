#include "ui.hpp"

#include "Nexus.h"
#include "imgui.h"
#include "companion.hpp"
#include "live_data_api.hpp"
#include "settings.hpp"
#include "tonic.hpp"
#include "tonic_ids.hpp"

#include <cstdio>

namespace voxtonic::ui {
namespace {

AddonAPI* api = nullptr;

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

    bool changed = false;

    ImGui::TextUnformatted("VoxTonic — cosmetic tonic auto re-press");
    ImGui::Separator();

    if (ImGui::Checkbox("Enable tonic auto re-press", &settings::enabled)) {
        settings::markChanged();
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
        "feature is fully inert (no performance cost).");

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
    bool mountUnlock = settings::mountUnlockEnabled;
    if (ImGui::Checkbox("Unequip tonic on mount press (PvE only)", &mountUnlock)) {
        settings::mountUnlockEnabled = mountUnlock;
        settings::markChanged();
        tonic::updateBindings(api);
        changed = true;
    }
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("Mount key (VK code)", &settings::mountUnlockKey)) {
        settings::markChanged();
        tonic::updateBindings(api);
        changed = true;
    }
    ImGui::TextDisabled(
        "Virtual-key code of your GW2 mount key (default 88 = X). While "
        "transformed on foot in PvE, pressing it unequips the tonic and then "
        "presses your GW2 Mount/Dismount bind so the mount goes through.");

    ImGui::Separator();
    ImGui::SetNextItemWidth(100.0f);
    if (ImGui::InputInt("Novelty bind id", &settings::noveltyBind)) {
        settings::markChanged();
        changed = true;
    }
    ImGui::TextDisabled("EGameBinds value of the bind to press. 162 = Equip/Unequip Novelty.");

    if (changed) {
        settings::saveIfChanged(false);
    }
}

}

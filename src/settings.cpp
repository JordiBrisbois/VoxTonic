#include "settings.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace voxtonic::settings {
namespace {

bool dirty = false;
std::chrono::steady_clock::time_point changedAt {};
constexpr auto saveDebounce = std::chrono::milliseconds {400};

std::filesystem::path settingsPath()
{
    return std::filesystem::current_path() / "addons" / "VoxTonic" / "settings.ini";
}

void parseLine(const std::string& line)
{
    const auto split = line.find('=');
    if (split == std::string::npos) return;
    const auto key = line.substr(0, split), value = line.substr(split + 1);
    try {
        if (key == "enabled") enabled = std::stoi(value) != 0;
        else if (key == "effect_id") effectId = static_cast<std::uint32_t>(std::stoul(value));
        else if (key == "scan_all") scanAll = std::stoi(value) != 0;
        else if (key == "enable_pve") enablePve = std::stoi(value) != 0;
        else if (key == "enable_competitive") enableCompetitive = std::stoi(value) != 0;
        else if (key == "repress_delay_ms") rePressDelayMs = std::stoi(value);
        else if (key == "mount_unlock_enabled") mountUnlockEnabled = std::stoi(value) != 0;
        else if (key == "mount_unlock_key") mountUnlockKey = std::stoi(value);
        else if (key == "novelty_bind") noveltyBind = std::stoi(value);
    } catch (...) { /* ignore invalid line */ }
}

} // namespace

bool enabled = false;
std::uint32_t effectId = 0;
bool scanAll = false;
bool enablePve = true;
bool enableCompetitive = false;
int rePressDelayMs = 2000;
bool mountUnlockEnabled = true;
int mountUnlockKey = 'X';
int noveltyBind = 162; // EGameBinds_ToyUseDefault (Equip/Unequip Novelty)

void load()
{
    std::ifstream file(settingsPath());
    std::string line;
    while (std::getline(file, line)) parseLine(line);
    rePressDelayMs = std::clamp(rePressDelayMs, 200, 30000);
    dirty = false;
}

void saveIfChanged(const bool force)
{
    if (!dirty) return;
    if (!force && std::chrono::steady_clock::now() - changedAt < saveDebounce) return;
    const auto path = settingsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) return;
    file << "enabled=" << enabled << '\n'
         << "effect_id=" << effectId << '\n'
         << "scan_all=" << scanAll << '\n'
         << "enable_pve=" << enablePve << '\n'
         << "enable_competitive=" << enableCompetitive << '\n'
         << "repress_delay_ms=" << rePressDelayMs << '\n'
         << "mount_unlock_enabled=" << mountUnlockEnabled << '\n'
         << "mount_unlock_key=" << mountUnlockKey << '\n'
         << "novelty_bind=" << noveltyBind << '\n';
    file.flush();
    if (!file) return;
    file.close();
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return;
    }
#else
    std::filesystem::rename(temporary, path, error);
    if (error) return;
#endif
    dirty = false;
}

bool changed() { return dirty; }

} // namespace voxtonic::settings

#include "mumble_link.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <mutex>

namespace voxtonic::mumble_link {
namespace {

struct MumbleContext {
    std::uint8_t serverAddress[28];
    std::uint32_t mapId;
    std::uint32_t mapType;
    std::uint32_t shardId;
    std::uint32_t instance;
    std::uint32_t buildId;
    std::uint32_t uiState;
    std::uint16_t compassWidth;
    std::uint16_t compassHeight;
    float compassRotation;
    float playerX;
    float playerY;
    float mapCenterX;
    float mapCenterY;
    float mapScale;
    std::uint32_t processId;
    std::uint32_t mountIndex;
};

struct LinkedMem {
    std::uint32_t uiVersion;
    std::uint32_t uiTick;
    float fAvatarPosition[3];
    float fAvatarFront[3];
    float fAvatarTop[3];
    wchar_t name[256];
    float fCameraPosition[3];
    float fCameraFront[3];
    float fCameraTop[3];
    wchar_t identity[256];
    std::uint32_t context_len;
    MumbleContext context;
    wchar_t description[2048];
};

struct HandleScope {
    HANDLE handle = nullptr;
    ~HandleScope() { if (handle) CloseHandle(handle); }
};

struct ViewScope {
    void* view = nullptr;
    ~ViewScope() { if (view) UnmapViewOfFile(view); }
};

constexpr std::uint32_t kMapTypeStructuredPvp = 2;
constexpr std::uint32_t kMapTypeWvw = 9;
// Last WvW-family map type (EotM and the other WvW variants sit above 9).
constexpr std::uint32_t kMapTypeWvwLast = 15;
// MumbleLink context uiState bits, validated in game (WvW, chat closed/open
// and in/out of combat): bit 5 (0x20) is the text input / chat focus, bit 6
// (0x40) is combat. The inherited assignment was off by one (it read chat on
// bit 6, combat on bit 7 and competitive on bit 5, so combat was never true,
// chat never matched and the bit-5 term was dead). Bit 7 was never seen set
// and is not used.
constexpr std::uint32_t kUiStateTextInput = 1u << 5;
constexpr std::uint32_t kUiStateInCombat = 1u << 6;

struct ContextSnapshot {
    std::uint32_t mapId = 0;
    std::uint32_t mapType = 0;
    int mount = 0;
    std::uint32_t uiState = 0;
};

// Called from both the render thread (tick) and the game's window thread
// (WndProc mount-unlock): the caches are mutex-guarded so a torn timestamp or
// half-written snapshot can never be observed.
std::mutex cacheMutex;

ContextSnapshot readContext()
{
    static ContextSnapshot cached {};
    static std::chrono::steady_clock::time_point cachedAt {};
    const auto now = std::chrono::steady_clock::now();
    {
        const std::lock_guard lock(cacheMutex);
        if (cachedAt != std::chrono::steady_clock::time_point {}
            && now - cachedAt < std::chrono::milliseconds {120}) {
            return cached;
        }
    }

    ContextSnapshot fresh {};
    HandleScope mapping {OpenFileMappingW(FILE_MAP_READ, FALSE, L"MumbleLink")};
    if (mapping.handle != nullptr) {
        ViewScope view {MapViewOfFile(mapping.handle, FILE_MAP_READ, 0, 0, sizeof(LinkedMem))};
        if (view.view != nullptr) {
            const auto* memory = static_cast<const LinkedMem*>(view.view);
            fresh.mapId = memory->context.mapId;
            fresh.mapType = memory->context.mapType;
            fresh.mount = static_cast<int>(memory->context.mountIndex);
            fresh.uiState = memory->context.uiState;
        }
    }
    const std::lock_guard lock(cacheMutex);
    cached = fresh;
    cachedAt = now;
    return cached;
}

}

Signals readSignals()
{
    const auto snap = readContext();
    Signals out;
    out.mount = snap.mount;
    out.mapId = snap.mapId;
    out.mapType = snap.mapType;
    out.uiState = snap.uiState;
    out.textInput = (snap.uiState & kUiStateTextInput) != 0;
    out.inCombat = (snap.uiState & kUiStateInCombat) != 0;
    out.competitive = snap.mapType == kMapTypeStructuredPvp
        || (snap.mapType >= kMapTypeWvw && snap.mapType <= kMapTypeWvwLast);
    return out;
}

bool isCompetitive()
{
    return readSignals().competitive;
}

int mountIndex()
{
    return readSignals().mount;
}

bool textInputFocused()
{
    return readSignals().textInput;
}

bool isInCombat()
{
    return readSignals().inCombat;
}

}

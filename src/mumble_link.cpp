#include "mumble_link.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cstdint>

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
    ~HandleScope()
    {
        if (handle) CloseHandle(handle);
    }
};

struct ViewScope {
    void* view = nullptr;
    ~ViewScope()
    {
        if (view) UnmapViewOfFile(view);
    }
};

// GW2 Mumble mapType values.
constexpr std::uint32_t kMapTypeStructuredPvp = 2;
constexpr std::uint32_t kMapTypeWvw = 9;       // incl. Edge of the Mists
constexpr std::uint32_t kMapTypeWvwEotm = 11;

// Fresh (cached < 250 ms) map type and mount index from the MumbleLink shared
// memory. Returns 0 for both when the link is unavailable.
struct ContextSnapshot {
    std::uint32_t mapType = 0;
    int mount = 0;
};

ContextSnapshot readContext()
{
    static ContextSnapshot cached {};
    static std::chrono::steady_clock::time_point cachedAt {};
    const auto now = std::chrono::steady_clock::now();
    if (cachedAt != std::chrono::steady_clock::time_point {}
        && now - cachedAt < std::chrono::milliseconds {250}) {
        return cached;
    }

    ContextSnapshot fresh {};
    HandleScope mapping {OpenFileMappingW(FILE_MAP_READ, FALSE, L"MumbleLink")};
    if (mapping.handle != nullptr) {
        ViewScope view {MapViewOfFile(mapping.handle, FILE_MAP_READ, 0, 0, sizeof(LinkedMem))};
        if (view.view != nullptr) {
            const auto* memory = static_cast<const LinkedMem*>(view.view);
            fresh.mapType = memory->context.mapType;
            fresh.mount = static_cast<int>(memory->context.mountIndex);
        }
    }
    cached = fresh;
    cachedAt = now;
    return cached;
}

} // namespace

bool isCompetitive()
{
    static bool cached = false;
    static std::chrono::steady_clock::time_point cachedAt {};
    const auto now = std::chrono::steady_clock::now();
    if (cachedAt != std::chrono::steady_clock::time_point {}
        && now - cachedAt < std::chrono::seconds {3}) {
        return cached;
    }

    const auto mapType = readContext().mapType;
    cached = mapType == kMapTypeStructuredPvp || mapType == kMapTypeWvw
        || mapType == kMapTypeWvwEotm;
    cachedAt = now;
    return cached;
}

int mountIndex()
{
    return readContext().mount;
}

} // namespace voxtonic::mumble_link


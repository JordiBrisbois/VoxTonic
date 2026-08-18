#include "companion.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>

namespace voxtonic::companion {
namespace {

std::atomic_bool active = false;
std::chrono::steady_clock::time_point lastPoll {};

constexpr auto pollInterval = std::chrono::milliseconds {750};

bool probe()
{
    if (GetModuleHandleW(L"VoxSake.dll") != nullptr) return true;
    if (GetModuleHandleW(L"VoxSake") != nullptr) return true;
    HMODULE found = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"VoxSake.dll", &found)
        && found != nullptr) return true;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, L"VoxSake", &found)
        && found != nullptr) return true;
    return false;
}

}

bool isActive() { return active.load(std::memory_order_acquire); }

void poll()
{
    const auto now = std::chrono::steady_clock::now();
    if (lastPoll != std::chrono::steady_clock::time_point {} && now - lastPoll < pollInterval) return;
    lastPoll = now;
    active.store(probe(), std::memory_order_release);
}

void reset()
{
    active.store(false, std::memory_order_release);
    lastPoll = {};
}

}

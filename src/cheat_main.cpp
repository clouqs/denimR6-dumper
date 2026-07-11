#include "chains.h"
#include "codecave.h"
#include "exporter.h"
#include "heap_scanner.h"
#include "kernel.h"
#include "math_util.h"
#include "overlay.h"
#include "process.h"
#include "resolver.h"
#include "scanner.h"
#include "validator.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <d3d11.h>

#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

std::wstring wideFromUtf8(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(size - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, result.data(), static_cast<int>(result.size()));
    return result;
}

struct CheatOffsets {
    uint64_t gameManagerRva = 0;
    uint64_t networkManagerRva = 0;
    uint64_t glowManagerRva = 0;
    uint64_t roundManagerRva = 0;
    uint64_t fovManagerRva = 0;

    uint32_t viewCameraPosition = 0;
    uint32_t viewProjection = 0;

    uint32_t gmEntityList = 0;
    uint32_t gmEntityCount = 0;
    uint32_t entityPosition = 0;
    uint32_t entityHealth = 0;
    uint32_t entityTeamId = 0;
};

uint32_t findProcessPid(const std::wstring& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    uint32_t pid = 0;

    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return pid;
}

} // namespace

int main() {
    std::cout << "========================================\n";
    std::cout << "  denimR6-cheat — R6 External ESP\n";
    std::cout << "========================================\n\n";

    std::cout << "[*] Opening kernel driver...\n";
    Kernel kernel;
    if (!kernel.open()) {
        std::cerr << "[ERROR] Kernel driver not available. Ensure the cheat driver is loaded.\n";
        return 1;
    }
    std::cout << "[+] Kernel driver connected.\n";

    const uint32_t pid = findProcessPid(L"RainbowSix.exe");
    if (pid == 0) {
        std::cerr << "[ERROR] RainbowSix.exe not found. Launch the game first.\n";
        return 1;
    }
    std::cout << "[+] Attached to RainbowSix.exe (PID " << pid << ")\n\n";

    const auto gameWindow = FindWindowW(
        L"UnrealWindow",
        L"Tom Clancy's Rainbow Six Siege");

    if (!gameWindow) {
        std::cerr << "[ERROR] Could not find R6 game window.\n";
        return 1;
    }

    RECT rect{};
    GetWindowRect(gameWindow, &rect);
    const int screenWidth = rect.right - rect.left;
    const int screenHeight = rect.bottom - rect.top;

    Overlay overlay;
    if (!overlay.init(gameWindow)) {
        std::cerr << "[ERROR] Failed to initialize overlay.\n";
        return 1;
    }

    std::cout << "[*] Starting ESP loop (screen: " << screenWidth << "x" << screenHeight << ")\n";

    const CheatOffsets offsets;

    auto lastFrame = std::chrono::steady_clock::now();

    while (true) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (msg.message == WM_QUIT) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        const float elapsed = std::chrono::duration<float>(now - lastFrame).count();
        if (elapsed < 0.03f) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        lastFrame = now;

        std::vector<EspEntity> entities;

        overlay.render(entities, {}, {});

        Sleep(1);
    }

    overlay.shutdown();
    std::cout << "[+] Cheat shut down.\n";
    return 0;
}

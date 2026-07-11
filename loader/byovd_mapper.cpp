#include "byovd_mapper.h"

#include <cstdio>
#include <fstream>
#include <iostream>

bool ByovdMapper::init(const std::string& vulnerableDriverPath) {
    std::wifstream wStream;
    wStream.imbue(std::locale(".UTF8"));

    std::wstring widePath(vulnerableDriverPath.begin(), vulnerableDriverPath.end());
    vulnHandle_ = CreateFileW(
        widePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (vulnHandle_ == INVALID_HANDLE_VALUE) {
        std::cerr << "[BYOVD] Failed to open vulnerable driver: " << vulnerableDriverPath << "\n";
        return false;
    }

    return true;
}

bool ByovdMapper::mapDriver(const std::string& cheatDriverPath) {
    std::ifstream file(cheatDriverPath, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "[BYOVD] Failed to open cheat driver: " << cheatDriverPath << "\n";
        return false;
    }

    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<uint8_t> driverBytes(size);
    if (!file.read(reinterpret_cast<char*>(driverBytes.data()), size)) {
        std::cerr << "[BYOVD] Failed to read cheat driver.\n";
        return false;
    }

    if (!exploitMap(driverBytes)) {
        return false;
    }

    mapped_ = true;
    printf("[+] Driver mapped at 0x%llX\n", mappedBase_);
    return true;
}

bool ByovdMapper::unmapDriver() {
    if (!mapped_) {
        return false;
    }

    mapped_ = false;
    return true;
}

void ByovdMapper::cleanup() {
    if (vulnHandle_ && vulnHandle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(vulnHandle_);
        vulnHandle_ = nullptr;
    }
    mapped_ = false;
    mappedBase_ = 0;
}

uintptr_t ByovdMapper::getVulnerableDriverAddress() const {
    return 0;
}

bool ByovdMapper::exploitMap(const std::vector<uint8_t>& driverBytes) {
    if (driverBytes.empty()) {
        return false;
    }

    mappedBase_ = reinterpret_cast<uintptr_t>(
        VirtualAlloc(nullptr, driverBytes.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

    if (!mappedBase_) {
        std::cerr << "[BYOVD] VirtualAlloc failed.\n";
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(mappedBase_), driverBytes.data(), driverBytes.size());
    return true;
}

bool ByovdMapper::fixImports(uintptr_t driverBase) {
    return true;
}

bool ByovdMapper::callDriverEntry(uintptr_t driverBase) {
    return true;
}

bool ByovdMapper::clearTrace(const std::string& traceName) {
    return true;
}

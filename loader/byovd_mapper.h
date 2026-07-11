#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

class ByovdMapper {
public:
    bool init(const std::string& vulnerableDriverPath);
    bool mapDriver(const std::string& cheatDriverPath);
    bool unmapDriver();
    void cleanup();

    [[nodiscard]] bool isMapped() const { return mapped_ ; }

private:
    HANDLE vulnHandle_ = nullptr;
    uintptr_t mappedBase_ = 0;
    bool mapped_ = false;

    uintptr_t getVulnerableDriverAddress() const;
    bool exploitMap(const std::vector<uint8_t>& driverBytes);
    bool fixImports(uintptr_t driverBase);
    bool callDriverEntry(uintptr_t driverBase);
    bool clearTrace(const std::string& traceName);
};

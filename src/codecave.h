#pragma once

#include "kernel.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct CodecaveConfig {
    std::string name;
    uint64_t    patchAddress = 0;
    std::vector<uint8_t> originalBytes;
    std::vector<uint8_t> patchedBytes;
    uint64_t    codecaveAddress = 0;
    size_t      codecaveSize = 0;
};

class Codecaves {
public:
    explicit Codecaves(Kernel& kernel);

    bool allocate(uint32_t pid, uint64_t moduleBase, size_t totalSize = 0x1000);
    void deallocate(uint32_t pid);

    [[nodiscard]] bool isAllocated() const { return baseAddress_ != 0; }
    [[nodiscard]] uint64_t baseAddress() const { return baseAddress_; }
    [[nodiscard]] uint32_t pid() const { return pid_; }

    bool patch(uint32_t pid, const CodecaveConfig& config);
    bool restore(uint32_t pid, CodecaveConfig& config);

    [[nodiscard]] const std::vector<CodecaveConfig>& codecs() const { return configs_; }

private:
    Kernel& kernel_;
    uint32_t pid_ = 0;
    uint64_t baseAddress_ = 0;
    size_t allocationSize_ = 0;
    std::vector<CodecaveConfig> configs_;
};

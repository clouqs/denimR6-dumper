#pragma once

#include "kernel.h"
#include "process.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class KernelProcess {
public:
    explicit KernelProcess(Kernel& kernel);

    bool attach(uint32_t pid);
    void detach();

    [[nodiscard]] bool isAttached() const { return pid_ != 0; }
    [[nodiscard]] uint32_t pid() const { return pid_; }

    [[nodiscard]] bool read(uint64_t address, void* buffer, size_t size) const;
    [[nodiscard]] std::vector<uint8_t> readBytes(uint64_t address, size_t size) const;

    template<typename T>
    [[nodiscard]] std::optional<T> read(uint64_t address) const {
        return kernel_.read<T>(pid_, address);
    }

    [[nodiscard]] std::optional<ModuleInfo> getModule(const std::wstring& moduleName) const;
    [[nodiscard]] std::vector<MemoryRegion> enumerateRegions() const;

private:
    Kernel& kernel_;
    uint32_t pid_ = 0;
};

#pragma once

#include <Windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ModuleInfo {
    std::wstring name;
    uint64_t base = 0;
    uint64_t size = 0;
};

struct MemoryRegion {
    uint64_t base = 0;
    uint64_t size = 0;
    uint32_t protect = 0;
};

class Process {
public:
    Process() = default;
    ~Process();

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    bool attach(const std::wstring& processName);
    void detach();

    [[nodiscard]] bool isAttached() const { return handle_ != nullptr; }
    [[nodiscard]] uint32_t pid() const { return pid_; }
    [[nodiscard]] HANDLE handle() const { return handle_; }

    [[nodiscard]] std::optional<ModuleInfo> getModule(const std::wstring& moduleName) const;
    [[nodiscard]] std::vector<MemoryRegion> enumerateRegions() const;

    [[nodiscard]] bool read(uint64_t address, void* buffer, size_t size) const;
    [[nodiscard]] std::vector<uint8_t> readBytes(uint64_t address, size_t size) const;

    template<typename T>
    [[nodiscard]] std::optional<T> read(uint64_t address) const {
        T value{};
        if (!read(address, &value, sizeof(T))) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::wstring exePath() const;
    [[nodiscard]] std::string exeVersion() const;
    [[nodiscard]] std::string exeSha256() const;

private:
    HANDLE handle_ = nullptr;
    uint32_t pid_ = 0;
};

#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <vector>

/*
 *  Kernel — usermode side of shared-memory communication.
 *
 *  Uses Windows Section objects and Events instead of IOCTLs:
 *  - open()   maps a named section and opens two events
 *  - read()   writes a request to the shared buffer, signals request event,
 *             waits for response event, copies result
 *  - write()  same flow for write requests
 *  - close()  unmaps the section and closes handles
 */

class Kernel {
public:
    Kernel() = default;
    ~Kernel();

    Kernel(const Kernel&) = delete;
    Kernel& operator=(const Kernel&) = delete;

    bool open();
    void close();

    [[nodiscard]] bool isOpen() const { return sectionView_ != nullptr; }

    [[nodiscard]] bool read(uint32_t pid, uint64_t address, void* buffer, size_t size) const;
    [[nodiscard]] bool write(uint32_t pid, uint64_t address, const void* buffer, size_t size) const;

    template<typename T>
    [[nodiscard]] std::optional<T> read(uint32_t pid, uint64_t address) const {
        T value{};
        if (!read(pid, address, &value, sizeof(T))) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::vector<uint8_t> readBytes(uint32_t pid, uint64_t address, size_t size) const;

private:
    HANDLE   sectionHandle_   = INVALID_HANDLE_VALUE;
    PVOID    sectionView_     = nullptr;
    HANDLE   requestEvent_    = INVALID_HANDLE_VALUE;
    HANDLE   responseEvent_   = INVALID_HANDLE_VALUE;
};

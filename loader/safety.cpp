#include "safety.h"

#include <cstdio>
#include <fstream>
#include <cstring>

namespace {

constexpr uintptr_t PAGE_SIZE = 0x1000;

struct ListEntry {
    uintptr_t flink;
    uintptr_t blink;
};

} // namespace

void Safety::clearAllTraces() {
    clearPiDDBCacheTable();
    clearMmUnloadedDrivers();
    clearWdFilter();
    clearKernelHashBucketList();
}

uintptr_t Safety::getSystemRoutineAddress(const std::string& routineName) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;

    FARPROC addr = GetProcAddress(ntdll, routineName.c_str());
    if (!addr) return 0;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(addr);
    if (bytes[0] == 0x4C && bytes[1] == 0x8B && bytes[2] == 0x0D) {
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes + 3, sizeof(displacement));
        return reinterpret_cast<uintptr_t>(bytes) + 7 + displacement;
    }

    return 0;
}

uintptr_t Safety::getKernelModule(const std::string& moduleName) {
    return 0;
}

std::vector<uint8_t> Safety::readDriverFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};

    const auto size = static_cast<size_t>(file.tellg());
    file.seekg(0);

    std::vector<uint8_t> data(size);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

bool Safety::clearPiDDBCacheTable() {
    return true;
}

bool Safety::clearMmUnloadedDrivers() {
    return true;
}

bool Safety::clearWdFilter() {
    return true;
}

bool Safety::clearKernelHashBucketList() {
    return true;
}

bool Safety::unlinkDriverFromList(uintptr_t driverBase) {
    return true;
}

#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>

class Safety {
public:
    static bool clearPiDDBCacheTable();
    static bool clearMmUnloadedDrivers();
    static bool clearWdFilter();
    static bool clearKernelHashBucketList();
    static void clearAllTraces();

    static bool unlinkDriverFromList(uintptr_t driverBase);

    static std::vector<uint8_t> readDriverFile(const std::string& path);

    static uintptr_t getSystemRoutineAddress(const std::string& routineName);

private:
    template<typename T>
    static bool clearLinkedList(uintptr_t listHead, uint16_t listOffset);

    static uintptr_t getKernelModule(const std::string& moduleName);
};

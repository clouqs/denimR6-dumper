#include "kernel_process.h"

#include <TlHelp32.h>

KernelProcess::KernelProcess(Kernel& kernel)
    : kernel_(kernel) {}

bool KernelProcess::attach(uint32_t pid) {
    pid_ = pid;
    return kernel_.open();
}

void KernelProcess::detach() {
    pid_ = 0;
}

bool KernelProcess::read(uint64_t address, void* buffer, size_t size) const {
    return kernel_.read(pid_, address, buffer, size);
}

std::vector<uint8_t> KernelProcess::readBytes(uint64_t address, size_t size) const {
    return kernel_.readBytes(pid_, address, size);
}

std::optional<ModuleInfo> KernelProcess::getModule(const std::wstring& moduleName) const {
    if (!isAttached()) {
        return std::nullopt;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid_);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    std::optional<ModuleInfo> result;

    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, moduleName.c_str()) == 0) {
                result = ModuleInfo{
                    entry.szModule,
                    reinterpret_cast<uint64_t>(entry.modBaseAddr),
                    entry.modBaseSize
                };
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

std::vector<MemoryRegion> KernelProcess::enumerateRegions() const {
    std::vector<MemoryRegion> regions;
    if (!isAttached()) {
        return regions;
    }

    uint64_t address = 0;
    MEMORY_BASIC_INFORMATION mbi{};

    while (VirtualQueryEx(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(-1)),
                          reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const bool committed = mbi.State == MEM_COMMIT;
        const bool readable = (mbi.Protect & PAGE_GUARD) == 0 &&
                              (mbi.Protect & PAGE_NOACCESS) == 0 &&
                              mbi.Protect != 0;

        if (committed && readable) {
            regions.push_back(MemoryRegion{
                reinterpret_cast<uint64_t>(mbi.BaseAddress),
                mbi.RegionSize,
                mbi.Protect
            });
        }

        const uint64_t next = reinterpret_cast<uint64_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= address) {
            break;
        }
        address = next;
    }

    return regions;
}

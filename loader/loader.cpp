#include <Windows.h>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>
#include <winternl.h>

#pragma comment(lib, "ntdll.lib")

namespace {

constexpr DWORD PAGE_SIZE = 0x1000;

using NTSTATUS = LONG;
using UNICODE_STRING = struct {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
};
using PUNICODE_STRING = UNICODE_STRING*;

using PNtCreateFile = NTSTATUS(WINAPI*)(
    PHANDLE,
    ACCESS_MASK,
    POBJECT_ATTRIBUTES,
    PIO_STATUS_BLOCK,
    PLARGE_INTEGER,
    ULONG,
    ULONG,
    ULONG,
    ULONG,
    PVOID,
    ULONG);

using PNtWriteFile = NTSTATUS(WINAPI*)(
    HANDLE,
    HANDLE,
    PVOID,
    PVOID,
    PIO_STATUS_BLOCK,
    PVOID,
    ULONG,
    PLARGE_INTEGER,
    PULONG);

using PNtMapViewOfSection = NTSTATUS(WINAPI*)(
    HANDLE,
    HANDLE,
    PVOID*,
    ULONG_PTR,
    SIZE_T,
    PLARGE_INTEGER,
    PSIZE_T,
    ULONG,
    ULONG,
    ULONG);

using PZwUnmapViewOfSection = NTSTATUS(WINAPI*)(
    HANDLE,
    PVOID);

using PZwFreeVirtualMemory = NTSTATUS(WINAPI*)(
    HANDLE,
    PVOID*,
    PSIZE_T,
    ULONG);

using PFN_GetModuleHandleW = HMODULE(WINAPI*)(PCWSTR);
using PFN_GetProcAddress = FARPROC(WINAPI*)(HMODULE, LPCSTR);
using PFN_LoadLibraryW = HMODULE(WINAPI*)(LPCWSTR);
using PFN_CreateFileMappingW = HANDLE(WINAPI*)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD, LPCWSTR);
using PFN_MapViewOfFile = LPVOID(WINAPI*)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);
using PFN_UnmapViewOfFile = BOOL(WINAPI*)(LPVOID);
using PFN_CloseHandle = BOOL(WINAPI*)(HANDLE);

struct DriverOffsets {
    uintptr_t sectionHeaderOffset = 0;
    uintptr_t entryPointOffset = 0;
    uintptr_t imageSizeOffset = 0;
    uintptr_t importDescriptorOffset = 0;
    uintptr_t textSizeOffset = 0;
};

uintptr_t getSystemRoutineAddress(PFN_GetModuleHandleW nGetModuleHandleW,
                                   PFN_GetProcAddress nGetProcAddress,
                                   const char* name) {
    HMODULE ntdll = nGetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;

    FARPROC addr = nGetProcAddress(ntdll, name);
    if (!addr) return 0;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(addr);
    if (bytes[0] == 0x4C && bytes[1] == 0x8B && bytes[2] == 0x0D) {
        int32_t displacement = 0;
        std::memcpy(&displacement, bytes + 3, sizeof(displacement));
        return reinterpret_cast<uintptr_t>(bytes) + 7 + displacement;
    }

    return 0;
}

bool mapDriver(PFN_GetModuleHandleW nGetModuleHandleW,
               PFN_GetProcAddress nGetProcAddress,
               PFN_LoadLibraryW nLoadLibraryW,
               PFN_CreateFileMappingW nCreateFileMappingW,
               PFN_MapViewOfFile nMapViewOfFile,
               PFN_UnmapViewOfFile nUnmapViewOfFile,
               PFN_CloseHandle nCloseHandle,
               const std::vector<uint8_t>& driverBytes,
               DriverOffsets* offsets,
               uintptr_t* outBase) {

    HANDLE mapping = nCreateFileMappingW(
        reinterpret_cast<HANDLE>(0xFFFFFFFFFFFFFFFF),
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(driverBytes.size()),
        nullptr);

    if (!mapping) return false;

    LPVOID view = nMapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    nCloseHandle(mapping);

    if (!view) return false;

    std::memcpy(view, driverBytes.data(), driverBytes.size());
    *outBase = reinterpret_cast<uintptr_t>(view);

    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(view);
    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uintptr_t>(view) + dos->e_lfanew);

    offsets->sectionHeaderOffset = reinterpret_cast<uintptr_t>(&nt->OptionalHeader) - reinterpret_cast<uintptr_t>(view);
    offsets->entryPointOffset = nt->OptionalHeader.AddressOfEntryPoint;
    offsets->imageSizeOffset = nt->OptionalHeader.SizeOfImage;
    offsets->importDescriptorOffset = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    offsets->textSizeOffset = nt->OptionalHeader.SizeOfCode;

    return true;
}

bool unmapDriver(PFN_UnmapViewOfFile nUnmapViewOfFile, uintptr_t base) {
    return nUnmapViewOfFile(reinterpret_cast<LPVOID>(base)) != FALSE;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        printf("Usage: loader.exe <driver.sys> [vuln_driver.sys]\n");
        return 1;
    }

    printf("[*] R6 Cheat Loader\n");
    printf("[*] ====================\n\n");

    std::wstring driverPath(argv[1] ? argv[1] : L"");
    printf("[*] Loading driver from: %ls\n", driverPath.c_str());

    printf("[!] TODO: Integrate BYOVD mapping logic.\n");
    printf("[!] This loader will be connected to a vulnerable driver from loldrivers.io\n");
    printf("[!] and will manually map the cheat driver into kernel memory.\n");

    return 0;
}

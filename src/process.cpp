#include "process.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "version.lib")

Process::~Process() {
    detach();
}

bool Process::attach(const std::wstring& processName) {
    detach();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return false;
    }

    do {
        if (_wcsicmp(entry.szExeFile, processName.c_str()) == 0) {
            pid_ = entry.th32ProcessID;
            break;
        }
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);

    if (pid_ == 0) {
        return false;
    }

    handle_ = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid_);
    return handle_ != nullptr;
}

void Process::detach() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    pid_ = 0;
}

std::optional<ModuleInfo> Process::getModule(const std::wstring& moduleName) const {
    if (!handle_) {
        return std::nullopt;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
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

std::vector<MemoryRegion> Process::enumerateRegions() const {
    std::vector<MemoryRegion> regions;
    if (!handle_) {
        return regions;
    }

    uint64_t address = 0;
    MEMORY_BASIC_INFORMATION mbi{};

    while (VirtualQueryEx(handle_, reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == sizeof(mbi)) {
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

bool Process::read(uint64_t address, void* buffer, size_t size) const {
    if (!handle_ || !buffer || size == 0) {
        return false;
    }

    SIZE_T bytesRead = 0;
    return ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), buffer, size, &bytesRead) &&
           bytesRead == size;
}

std::vector<uint8_t> Process::readBytes(uint64_t address, size_t size) const {
    std::vector<uint8_t> buffer(size);
    if (!read(address, buffer.data(), size)) {
        buffer.clear();
    }
    return buffer;
}

std::wstring Process::exePath() const {
    if (!handle_) {
        return {};
    }

    std::wstring path(MAX_PATH, L'\0');
    DWORD size = static_cast<DWORD>(path.size());

    if (QueryFullProcessImageNameW(handle_, 0, path.data(), &size)) {
        path.resize(size);
        return path;
    }

    return {};
}

std::string Process::exeVersion() const {
    const std::wstring path = exePath();
    if (path.empty()) {
        return "unknown";
    }

    DWORD handle = 0;
    const DWORD versionSize = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (versionSize == 0) {
        return "unknown";
    }

    std::vector<uint8_t> data(versionSize);
    if (!GetFileVersionInfoW(path.c_str(), 0, versionSize, data.data())) {
        return "unknown";
    }

    VS_FIXEDFILEINFO* fileInfo = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&fileInfo), &len) || len == 0) {
        return "unknown";
    }

    const WORD major = HIWORD(fileInfo->dwFileVersionMS);
    const WORD minor = LOWORD(fileInfo->dwFileVersionMS);
    const WORD build = HIWORD(fileInfo->dwFileVersionLS);
    const WORD revision = LOWORD(fileInfo->dwFileVersionLS);

    std::ostringstream oss;
    oss << major << '.' << minor << '.' << build << '.' << revision;
    return oss.str();
}

std::string Process::exeSha256() const {
    const std::wstring path = exePath();
    if (path.empty()) {
        return "unknown";
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "unknown";
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return "unknown";
    }

    DWORD hashObjectSize = 0;
    DWORD hashDataSize = 0;
    DWORD cbData = 0;

    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashObjectSize), sizeof(hashObjectSize),
                          &cbData, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return "unknown";
    }

    if (BCryptGetProperty(alg, BCRYPT_HASH_LENGTH,
                          reinterpret_cast<PUCHAR>(&hashDataSize), sizeof(hashDataSize),
                          &cbData, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return "unknown";
    }

    std::vector<uint8_t> hashObject(hashObjectSize);
    std::vector<uint8_t> hashData(hashDataSize);

    if (BCryptCreateHash(alg, &hash, hashObject.data(), hashObjectSize, nullptr, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return "unknown";
    }

    std::vector<char> buffer(1 << 20);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = file.gcount();
        if (got <= 0) {
            break;
        }
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(got), 0) != 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(alg, 0);
            return "unknown";
        }
    }

    if (BCryptFinishHash(hash, hashData.data(), hashDataSize, 0) != 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(alg, 0);
        return "unknown";
    }

    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);

    static const char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(hashData.size() * 2);
    for (uint8_t byte : hashData) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0F]);
    }
    return result;
}

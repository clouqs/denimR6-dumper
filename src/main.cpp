#include "chains.h"
#include "exporter.h"
#include "heap_scanner.h"
#include "process.h"
#include "resolver.h"
#include "scanner.h"
#include "validator.h"

#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }

    std::string utf8(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string getExecutableDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return wideToUtf8(std::filesystem::path(path).parent_path().wstring());
}

std::string currentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &time);

    std::ostringstream oss;
    oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

void printHeader() {
    std::cout << "========================================\n";
    std::cout << "  denimR6-dumper — R6 Offset Dumper\n";
    std::cout << "========================================\n\n";
}

void printManagerResult(const ManagerResult& manager) {
    const char* status = manager.valid ? "OK" : (manager.found ? "INVALID" : "NOT FOUND");
    std::cout << "  [" << status << "] " << manager.name;

    if (manager.valid) {
        std::cout << "  RVA=" << std::hex << std::uppercase << manager.resolve.rva
                  << "  ptr=0x" << manager.resolve.dereferenced << std::dec;
    } else if (!manager.error.empty()) {
        std::cout << "  (" << manager.error << ")";
    }

    std::cout << "\n";
}

} // namespace

int main() {
    printHeader();

    const std::string baseDir = getExecutableDir();
    const std::string globalsPath = baseDir + "/signatures/globals.json";
    const std::string heapPath = baseDir + "/signatures/heap.json";
    const std::string structsPath = baseDir + "/structs/offsets.json";
    const std::string outputDir = baseDir + "/output";

    const auto globalDb = loadGlobalSignatures(globalsPath);
    if (!globalDb) {
        std::cerr << "[ERROR] Failed to load " << globalsPath << "\n";
        return 1;
    }

    const auto heapSig = loadHeapSignature(heapPath);
    if (!heapSig) {
        std::cerr << "[ERROR] Failed to load " << heapPath << "\n";
        return 1;
    }

    const auto structOffsets = loadStructOffsets(structsPath);
    if (!structOffsets) {
        std::cerr << "[ERROR] Failed to load " << structsPath << "\n";
        return 1;
    }

    Process process;
    std::wcout << L"[*] Attaching to " << globalDb->moduleName << L"...\n";

    if (!process.attach(globalDb->moduleName)) {
        std::cerr << "[ERROR] Could not find RainbowSix.exe. Launch the game with -belaunch -be and enter a match.\n";
        return 1;
    }

    std::wcout << L"[+] Attached to PID " << process.pid() << L"\n";

    const auto module = process.getModule(globalDb->moduleName);
    if (!module) {
        std::cerr << "[ERROR] Failed to resolve module base for RainbowSix.exe\n";
        return 1;
    }

    std::cout << "[+] Module base: 0x" << std::hex << std::uppercase << module->base
              << "  size: 0x" << module->size << std::dec << "\n";
    std::cout << "[+] Exe version: " << process.exeVersion() << "\n";
    std::cout << "[*] SHA256: " << process.exeSha256() << "\n\n";

    std::cout << "[*] Scanning .text signatures...\n";
    std::vector<ManagerResult> managers;

    for (const GlobalSignature& sig : globalDb->signatures) {
        ManagerResult result;
        result.name = sig.name;
        result.critical = sig.critical;

        if (const auto match = scanPattern(process, module->base, module->size, sig.pattern)) {
            if (const auto resolved = resolveRipRelative(
                    process, *match, module->base, sig.dispOffset, sig.instrLength)) {
                result.resolve = *resolved;
            }
        }

        managers.push_back(result);
    }

    std::cout << "\n[*] Scanning heap for ViewData...\n";
    const auto viewData = findViewData(process, *heapSig);

    std::cout << "\n[*] Walking entity chain...\n";
    EntityChainResult entities;

    for (const ManagerResult& manager : managers) {
        if (manager.name == "GameManager" && manager.resolve.dereferenced != 0) {
            entities = walkEntityChain(process, manager.resolve.dereferenced, *structOffsets);
            break;
        }
    }

    if (entities.gameManager == 0) {
        entities.errors.push_back("GameManager not resolved — cannot walk entity chain");
    }

    const ValidationReport report = runValidation(process, managers, viewData, entities);

    std::cout << "\n--- Results ---\n\n";
    std::cout << "Managers:\n";
    for (const ManagerResult& manager : report.managers) {
        printManagerResult(manager);
    }

    std::cout << "\nViewData:\n";
    if (report.viewDataFound) {
        std::cout << "  [" << (report.viewDataValid ? "OK" : "INVALID") << "] base=0x"
                  << std::hex << report.viewData.baseAddress << std::dec
                  << "  camera=(" << report.viewData.cameraX << ", "
                  << report.viewData.cameraY << ", " << report.viewData.cameraZ << ")\n";
    } else {
        std::cout << "  [NOT FOUND]\n";
    }

    std::cout << "\nEntities:\n";
    std::cout << "  [" << (report.entitiesValid ? "OK" : "INVALID") << "] count="
              << report.entities.entityCount
              << "  samples=" << report.entities.samples.size() << "\n";

    if (!report.failures.empty()) {
        std::cout << "\nFailures:\n";
        for (const std::string& failure : report.failures) {
            std::cout << "  - " << failure << "\n";
        }
    }

    bool hasCriticalFailure = false;
    for (const ManagerResult& manager : report.managers) {
        if (manager.critical && !manager.valid) {
            hasCriticalFailure = true;
            break;
        }
    }

    if (!report.viewDataValid) {
        hasCriticalFailure = true;
    }

    if (hasCriticalFailure) {
        std::cout << "\n[ERROR] Critical validation failed. Update signatures/ or structs/ and re-run.\n";
        return 2;
    }

    DumpResult dump;
    dump.module = wideToUtf8(globalDb->moduleName);
    dump.version = process.exeVersion();
    dump.sha256 = process.exeSha256();
    dump.timestamp = currentTimestamp();
    dump.validation = report;

    if (!exportDump(dump, outputDir)) {
        std::cerr << "\n[ERROR] Failed to write output files to " << outputDir << "\n";
        return 1;
    }

    std::cout << "\n[+] Exported offsets to " << outputDir << "/offsets.json\n";
    std::cout << "[+] Exported offsets to " << outputDir << "/offsets.h\n";

    if (!report.entitiesValid) {
        std::cout << "\n[WARN] Entity validation failed — globals and ViewData were exported, but struct offsets may need updating.\n";
        return 3;
    }

    std::cout << "\n[+] Dump completed successfully.\n";
    return 0;
}

#pragma once

#include "process.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
};

struct GlobalSignature {
    std::string name;
    Pattern pattern;
    int dispOffset = 3;
    int instrLength = 7;
    bool critical = false;
};

struct GlobalSignatureDb {
    std::wstring moduleName;
    std::vector<GlobalSignature> signatures;
};

struct HeapSignature {
    std::string name;
    Pattern pattern;
    uint32_t anchorOffset = 0;
    uint32_t cameraPosition = 0;
    uint32_t viewProjection = 0;
};

struct StructOffsets {
    uint32_t gmEntityList = 0;
    uint32_t gmEntityCount = 0;
    uint32_t entityPosition = 0;
    uint32_t entityHealth = 0;
    uint32_t entityTeamId = 0;
    uint32_t entityNamePtr = 0;
    uint32_t entityBoneComponent = 0;
    uint32_t viewCameraPosition = 0;
    uint32_t viewProjection = 0;
    uint32_t viewAnchorPattern = 0;
};

[[nodiscard]] Pattern parsePattern(const std::string& patternText);
[[nodiscard]] std::optional<GlobalSignatureDb> loadGlobalSignatures(const std::string& path);
[[nodiscard]] std::optional<HeapSignature> loadHeapSignature(const std::string& path);
[[nodiscard]] std::optional<StructOffsets> loadStructOffsets(const std::string& path);

[[nodiscard]] std::optional<uint64_t> scanPattern(
    const Process& process,
    uint64_t base,
    uint64_t size,
    const Pattern& pattern);

[[nodiscard]] std::optional<uint64_t> scanPatternInRegions(
    const Process& process,
    const std::vector<MemoryRegion>& regions,
    const Pattern& pattern);

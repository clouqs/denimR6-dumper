#include "scanner.h"

#include "json_util.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {

uint8_t parseHexNibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(10 + c - 'a');
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(10 + c - 'A');
    return 0;
}

bool patternMatches(const uint8_t* data, size_t dataSize, const Pattern& pattern, size_t offset) {
    if (offset + pattern.bytes.size() > dataSize) {
        return false;
    }

    for (size_t i = 0; i < pattern.bytes.size(); ++i) {
        if (pattern.mask[i] && data[offset + i] != pattern.bytes[i]) {
            return false;
        }
    }

    return true;
}

std::optional<uint64_t> scanBuffer(const uint8_t* data, size_t size, uint64_t base, const Pattern& pattern) {
    if (pattern.bytes.empty() || size < pattern.bytes.size()) {
        return std::nullopt;
    }

    for (size_t i = 0; i <= size - pattern.bytes.size(); ++i) {
        if (patternMatches(data, size, pattern, i)) {
            return base + i;
        }
    }

    return std::nullopt;
}

} // namespace

Pattern parsePattern(const std::string& patternText) {
    Pattern pattern;
    std::istringstream stream(patternText);
    std::string token;

    while (stream >> token) {
        if (token == "?" || token == "??") {
            pattern.bytes.push_back(0);
            pattern.mask.push_back(false);
        } else if (token.size() == 2) {
            const uint8_t byte = static_cast<uint8_t>((parseHexNibble(token[0]) << 4) | parseHexNibble(token[1]));
            pattern.bytes.push_back(byte);
            pattern.mask.push_back(true);
        }
    }

    return pattern;
}

std::optional<GlobalSignatureDb> loadGlobalSignatures(const std::string& path) {
    const auto root = parseJsonFile(path);
    if (!root || !root->isObject()) {
        return std::nullopt;
    }

    GlobalSignatureDb db;
    if (const auto module = root->getString("module")) {
        db.moduleName.assign(module->begin(), module->end());
    }

    const JsonValue* signatures = root->get("signatures");
    if (!signatures || !signatures->isArray()) {
        return std::nullopt;
    }

    for (const JsonValue& entry : signatures->arrayValue) {
        if (!entry.isObject()) {
            continue;
        }

        GlobalSignature sig;
        if (const auto name = entry.getString("name")) {
            sig.name = *name;
        }
        if (const auto pattern = entry.getString("pattern")) {
            sig.pattern = parsePattern(*pattern);
        }
        if (const auto disp = entry.getNumber("disp_offset")) {
            sig.dispOffset = static_cast<int>(*disp);
        }
        if (const auto len = entry.getNumber("instr_length")) {
            sig.instrLength = static_cast<int>(*len);
        }
        if (const auto critical = entry.getBool("critical")) {
            sig.critical = *critical;
        }

        if (!sig.name.empty() && !sig.pattern.bytes.empty()) {
            db.signatures.push_back(std::move(sig));
        }
    }

    return db;
}

std::optional<HeapSignature> loadHeapSignature(const std::string& path) {
    const auto root = parseJsonFile(path);
    if (!root || !root->isObject()) {
        return std::nullopt;
    }

    HeapSignature sig;
    if (const auto name = root->getString("name")) {
        sig.name = *name;
    }
    if (const auto pattern = root->getString("pattern")) {
        sig.pattern = parsePattern(*pattern);
    }
    if (const auto anchor = root->getNumber("anchor_offset")) {
        sig.anchorOffset = static_cast<uint32_t>(*anchor);
    }

    const JsonValue* members = root->get("members");
    if (members && members->isObject()) {
        if (const auto camera = members->getNumber("CameraPosition")) {
            sig.cameraPosition = static_cast<uint32_t>(*camera);
        }
        if (const auto vp = members->getNumber("ViewProjection")) {
            sig.viewProjection = static_cast<uint32_t>(*vp);
        }
    }

    if (sig.pattern.bytes.empty()) {
        return std::nullopt;
    }

    return sig;
}

std::optional<StructOffsets> loadStructOffsets(const std::string& path) {
    const auto root = parseJsonFile(path);
    if (!root || !root->isObject()) {
        return std::nullopt;
    }

    StructOffsets offsets;

    const JsonValue* chains = root->get("chains");
    if (chains && chains->isObject()) {
        const JsonValue* gm = chains->get("GameManager");
        if (gm && gm->isObject()) {
            if (const auto v = gm->getNumber("entity_list")) offsets.gmEntityList = static_cast<uint32_t>(*v);
            if (const auto v = gm->getNumber("entity_count")) offsets.gmEntityCount = static_cast<uint32_t>(*v);
        }

        const JsonValue* entity = chains->get("Entity");
        if (entity && entity->isObject()) {
            if (const auto v = entity->getNumber("position")) offsets.entityPosition = static_cast<uint32_t>(*v);
            if (const auto v = entity->getNumber("health")) offsets.entityHealth = static_cast<uint32_t>(*v);
            if (const auto v = entity->getNumber("team_id")) offsets.entityTeamId = static_cast<uint32_t>(*v);
            if (const auto v = entity->getNumber("name_ptr")) offsets.entityNamePtr = static_cast<uint32_t>(*v);
            if (const auto v = entity->getNumber("bone_component")) offsets.entityBoneComponent = static_cast<uint32_t>(*v);
        }
    }

    const JsonValue* viewdata = root->get("viewdata");
    if (viewdata && viewdata->isObject()) {
        if (const auto v = viewdata->getNumber("CameraPosition")) offsets.viewCameraPosition = static_cast<uint32_t>(*v);
        if (const auto v = viewdata->getNumber("ViewProjection")) offsets.viewProjection = static_cast<uint32_t>(*v);
        if (const auto v = viewdata->getNumber("AnchorPattern")) offsets.viewAnchorPattern = static_cast<uint32_t>(*v);
    }

    return offsets;
}

std::optional<uint64_t> scanPattern(
    const Process& process,
    uint64_t base,
    uint64_t size,
    const Pattern& pattern) {

    if (pattern.bytes.empty() || size < pattern.bytes.size()) {
        return std::nullopt;
    }

    constexpr size_t kChunkSize = 1 << 20;
    const size_t overlap = pattern.bytes.size() > 0 ? pattern.bytes.size() - 1 : 0;
    std::vector<uint8_t> carry;

    for (uint64_t offset = 0; offset < size;) {
        const uint64_t remaining = size - offset;
        const size_t readSize = static_cast<size_t>(std::min<uint64_t>(kChunkSize, remaining));
        auto chunk = process.readBytes(base + offset, readSize);
        if (chunk.empty()) {
            offset += readSize;
            continue;
        }

        std::vector<uint8_t> buffer;
        buffer.reserve(carry.size() + chunk.size());
        buffer.insert(buffer.end(), carry.begin(), carry.end());
        buffer.insert(buffer.end(), chunk.begin(), chunk.end());

        if (const auto match = scanBuffer(buffer.data(), buffer.size(), base + offset - carry.size(), pattern)) {
            return match;
        }

        if (chunk.size() >= overlap) {
            carry.assign(chunk.end() - static_cast<std::ptrdiff_t>(overlap), chunk.end());
        } else {
            carry = std::move(chunk);
        }

        offset += readSize;
    }

    if (!carry.empty()) {
        return scanBuffer(carry.data(), carry.size(), base + size - carry.size(), pattern);
    }

    return std::nullopt;
}

std::optional<uint64_t> scanPatternInRegions(
    const Process& process,
    const std::vector<MemoryRegion>& regions,
    const Pattern& pattern) {

    for (const MemoryRegion& region : regions) {
        if (const auto match = scanPattern(process, region.base, region.size, pattern)) {
            return match;
        }
    }

    return std::nullopt;
}

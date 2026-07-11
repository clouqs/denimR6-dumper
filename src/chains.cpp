#include "chains.h"

#include "validator.h"

#include <cmath>
#include <string>

namespace {

bool isReadableEntityPtr(uint64_t ptr) {
    return ptr > 0x10000 && ptr < 0x7FFFFFFFFFFFULL;
}

} // namespace

EntityChainResult walkEntityChain(
    const Process& process,
    uint64_t gameManagerPtr,
    const StructOffsets& offsets) {

    EntityChainResult result;
    result.gameManager = gameManagerPtr;

    if (!isReadableEntityPtr(gameManagerPtr)) {
        result.errors.push_back("GameManager pointer is null or invalid");
        return result;
    }

    const auto entityListPtr = process.read<uint64_t>(gameManagerPtr + offsets.gmEntityList);
    if (!entityListPtr || !isReadableEntityPtr(*entityListPtr)) {
        result.errors.push_back("Failed to read entity list pointer at GameManager+0x" +
                                std::to_string(offsets.gmEntityList));
        return result;
    }

    result.entityList = *entityListPtr;

    const auto entityCount = process.read<uint32_t>(gameManagerPtr + offsets.gmEntityCount);
    if (!entityCount) {
        result.errors.push_back("Failed to read entity count at GameManager+0x" +
                                std::to_string(offsets.gmEntityCount));
        return result;
    }

    result.entityCount = *entityCount;

    const uint32_t sampleCount = std::min<uint32_t>(result.entityCount, 8);
    for (uint32_t i = 0; i < sampleCount; ++i) {
        const auto entityPtr = process.read<uint64_t>(result.entityList + static_cast<uint64_t>(i) * 8);
        if (!entityPtr || !isReadableEntityPtr(*entityPtr)) {
            continue;
        }

        EntitySample sample;
        sample.address = *entityPtr;

        const auto position = process.readBytes(*entityPtr + offsets.entityPosition, 12);
        if (position.size() == 12) {
            std::memcpy(&sample.x, position.data(), 4);
            std::memcpy(&sample.y, position.data() + 4, 4);
            std::memcpy(&sample.z, position.data() + 8, 4);
        }

        if (const auto health = process.read<float>(*entityPtr + offsets.entityHealth)) {
            sample.health = *health;
        }

        if (const auto team = process.read<int32_t>(*entityPtr + offsets.entityTeamId)) {
            sample.teamId = *team;
        }

        if (isFiniteFloat(sample.x) && isFiniteFloat(sample.y) && isFiniteFloat(sample.z)) {
            result.samples.push_back(sample);
        }
    }

    if (result.samples.empty()) {
        result.errors.push_back("No entities with valid positions found — update structs/offsets.json");
    }

    return result;
}

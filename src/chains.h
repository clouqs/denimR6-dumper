#pragma once

#include "process.h"
#include "scanner.h"

#include <cstdint>
#include <string>
#include <vector>

struct EntitySample {
    uint64_t address = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float health = 0.0f;
    int32_t teamId = 0;
};

struct EntityChainResult {
    uint64_t gameManager = 0;
    uint64_t entityList = 0;
    uint32_t entityCount = 0;
    std::vector<EntitySample> samples;
    std::vector<std::string> errors;
};

[[nodiscard]] EntityChainResult walkEntityChain(
    const Process& process,
    uint64_t gameManagerPtr,
    const StructOffsets& offsets);

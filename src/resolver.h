#pragma once

#include "process.h"

#include <cstdint>
#include <optional>

struct RipResolveResult {
    uint64_t instructionAddress = 0;
    uint64_t globalAddress = 0;
    uint64_t rva = 0;
    uint64_t dereferenced = 0;
};

[[nodiscard]] std::optional<RipResolveResult> resolveRipRelative(
    const Process& process,
    uint64_t matchAddress,
    uint64_t moduleBase,
    int dispOffset,
    int instrLength);

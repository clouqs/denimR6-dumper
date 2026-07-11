#pragma once

#include "process.h"
#include "scanner.h"

#include <cstdint>
#include <optional>

struct ViewDataResult {
    uint64_t patternAddress = 0;
    uint64_t baseAddress = 0;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float cameraZ = 0.0f;
    float viewMatrix[16]{};
};

[[nodiscard]] std::optional<ViewDataResult> findViewData(
    const Process& process,
    const HeapSignature& signature);

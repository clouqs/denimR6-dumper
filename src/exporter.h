#pragma once

#include "validator.h"

#include <string>

struct DumpResult {
    std::string game = "RainbowSix";
    std::string module;
    std::string version;
    std::string sha256;
    std::string timestamp;
    StructOffsets structOffsets{};
    ValidationReport validation;
};

[[nodiscard]] bool exportDump(const DumpResult& dump, const std::string& outputDir);

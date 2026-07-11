#pragma once

#include "chains.h"
#include "heap_scanner.h"
#include "resolver.h"
#include "scanner.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct ManagerResult {
    std::string name;
    bool found = false;
    bool valid = false;
    bool critical = false;
    RipResolveResult resolve{};
    std::string error;
};

struct ValidationReport {
    std::vector<ManagerResult> managers;
    bool viewDataFound = false;
    bool viewDataValid = false;
    ViewDataResult viewData{};
    EntityChainResult entities{};
    bool entitiesValid = false;
    std::vector<std::string> failures;
};

[[nodiscard]] bool isFiniteFloat(float value);
[[nodiscard]] bool validateManager(const Process& process, ManagerResult& manager);
[[nodiscard]] bool validateViewData(const ViewDataResult& viewData, int screenWidth, int screenHeight);
[[nodiscard]] bool validateEntities(const EntityChainResult& entities, const ViewDataResult* viewData);
[[nodiscard]] ValidationReport runValidation(
    const Process& process,
    std::vector<ManagerResult>& managers,
    const std::optional<ViewDataResult>& viewData,
    const EntityChainResult& entities,
    int screenWidth = 1920,
    int screenHeight = 1080);

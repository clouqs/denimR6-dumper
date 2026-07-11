#include "validator.h"

#include <cmath>
#include <cstring>

bool isFiniteFloat(float value) {
    return std::isfinite(value) && !std::isnan(value);
}

bool validateManager(const Process& process, ManagerResult& manager) {
    manager.found = manager.resolve.instructionAddress != 0;
    manager.valid = false;

    if (!manager.found) {
        manager.error = "Signature not found";
        return false;
    }

    if (manager.resolve.globalAddress == 0) {
        manager.error = "Failed to resolve global address";
        return false;
    }

    if (manager.resolve.dereferenced == 0) {
        manager.error = "Dereferenced manager pointer is null — are you in a match?";
        return false;
    }

    const auto probe = process.read<uint64_t>(manager.resolve.dereferenced);
    if (!probe) {
        manager.error = "Manager pointer is not readable";
        return false;
    }

    manager.valid = true;
    return true;
}

bool validateViewData(const ViewDataResult& viewData, int screenWidth, int screenHeight) {
    if (!isFiniteFloat(viewData.cameraX) ||
        !isFiniteFloat(viewData.cameraY) ||
        !isFiniteFloat(viewData.cameraZ)) {
        return false;
    }

    const float magnitude = std::sqrt(
        viewData.cameraX * viewData.cameraX +
        viewData.cameraY * viewData.cameraY +
        viewData.cameraZ * viewData.cameraZ);

    if (magnitude < 0.01f || magnitude > 100000.0f) {
        return false;
    }

    for (float m : viewData.viewMatrix) {
        if (!isFiniteFloat(m)) {
            return false;
        }
    }

    const float* m = viewData.viewMatrix;
    const float x = viewData.cameraX;
    const float y = viewData.cameraY;
    const float z = viewData.cameraZ;

    const float clipX = x * m[0] + y * m[4] + z * m[8] + m[12];
    const float clipY = x * m[1] + y * m[5] + z * m[9] + m[13];
    const float clipW = x * m[3] + y * m[7] + z * m[11] + m[15];

    if (std::fabs(clipW) < 0.0001f) {
        return false;
    }

    const float ndcX = clipX / clipW;
    const float ndcY = clipY / clipW;

    if (!isFiniteFloat(ndcX) || !isFiniteFloat(ndcY)) {
        return false;
    }

    const float screenX = static_cast<float>(screenWidth) * 0.5f + ndcX * static_cast<float>(screenWidth) * 0.5f;
    const float screenY = static_cast<float>(screenHeight) * 0.5f - ndcY * static_cast<float>(screenHeight) * 0.5f;

    if (!isFiniteFloat(screenX) || !isFiniteFloat(screenY)) {
        return false;
    }

    return screenX >= -static_cast<float>(screenWidth) &&
           screenX <= static_cast<float>(screenWidth) * 2.0f &&
           screenY >= -static_cast<float>(screenHeight) &&
           screenY <= static_cast<float>(screenHeight) * 2.0f;
}

bool validateEntities(const EntityChainResult& entities, const ViewDataResult* viewData) {
    if (entities.entityCount == 0) {
        return false;
    }

    if (entities.samples.empty()) {
        return false;
    }

    if (!viewData) {
        return true;
    }

    for (const EntitySample& sample : entities.samples) {
        const float dx = sample.x - viewData->cameraX;
        const float dy = sample.y - viewData->cameraY;
        const float dz = sample.z - viewData->cameraZ;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

        if (distance < 5000.0f) {
            return true;
        }
    }

    return false;
}

ValidationReport runValidation(
    const Process& process,
    std::vector<ManagerResult>& managers,
    const std::optional<ViewDataResult>& viewData,
    const EntityChainResult& entities,
    int screenWidth,
    int screenHeight) {

    ValidationReport report;
    report.managers = managers;
    report.entities = entities;

    for (ManagerResult& manager : report.managers) {
        (void)validateManager(process, manager);
        if (!manager.valid && manager.critical) {
            report.failures.push_back("Critical manager failed: " + manager.name + " — " + manager.error);
        }
    }

    if (viewData) {
        report.viewDataFound = true;
        report.viewData = *viewData;
        report.viewDataValid = validateViewData(*viewData, screenWidth, screenHeight);
        if (!report.viewDataValid) {
            report.failures.push_back("ViewData validation failed — camera or VP matrix invalid");
        }
    } else {
        report.failures.push_back("ViewData not found — ensure you are in an active match");
    }

    report.entitiesValid = validateEntities(entities, viewData ? &*viewData : nullptr);
    if (!report.entitiesValid) {
        if (!entities.errors.empty()) {
            report.failures.push_back(entities.errors.front());
        } else {
            report.failures.push_back("Entity chain validation failed — update structs/offsets.json");
        }
    }

    return report;
}

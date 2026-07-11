#include "heap_scanner.h"

#include "validator.h"

#include <cmath>

namespace {

bool readViewDataFields(const Process& process, uint64_t base, const HeapSignature& signature, ViewDataResult& out) {
    out.baseAddress = base;

    const auto camera = process.readBytes(base + signature.cameraPosition, 12);
    if (camera.size() != 12) {
        return false;
    }

    std::memcpy(&out.cameraX, camera.data(), 4);
    std::memcpy(&out.cameraY, camera.data() + 4, 4);
    std::memcpy(&out.cameraZ, camera.data() + 8, 4);

    const auto matrix = process.readBytes(base + signature.viewProjection, sizeof(out.viewMatrix));
    if (matrix.size() != sizeof(out.viewMatrix)) {
        return false;
    }

    std::memcpy(out.viewMatrix, matrix.data(), sizeof(out.viewMatrix));
    return true;
}

} // namespace

std::optional<ViewDataResult> findViewData(
    const Process& process,
    const HeapSignature& signature) {

    const auto regions = process.enumerateRegions();
    std::vector<ViewDataResult> candidates;

    for (const MemoryRegion& region : regions) {
        if (region.size < signature.pattern.bytes.size()) {
            continue;
        }

        uint64_t searchBase = region.base;
        uint64_t remaining = region.size;

        while (remaining >= signature.pattern.bytes.size()) {
            const auto match = scanPattern(process, searchBase, remaining, signature.pattern);
            if (!match) {
                break;
            }

            if (match < searchBase) {
                break;
            }

            const uint64_t offsetInRegion = *match - region.base;
            if (offsetInRegion < signature.anchorOffset) {
                searchBase = *match + 1;
                remaining = region.size - (searchBase - region.base);
                continue;
            }

            const uint64_t viewDataBase = *match - signature.anchorOffset;
            ViewDataResult candidate{};
            candidate.patternAddress = *match;

            if (readViewDataFields(process, viewDataBase, signature, candidate) &&
                validateViewData(candidate, 1920, 1080)) {
                candidates.push_back(candidate);
            }

            searchBase = *match + 1;
            remaining = region.size - (searchBase - region.base);
        }
    }

    if (candidates.empty()) {
        return std::nullopt;
    }

    return candidates.front();
}

#include "resolver.h"

#include <cmath>
#include <cstring>

std::optional<RipResolveResult> resolveRipRelative(
    const Process& process,
    uint64_t matchAddress,
    uint64_t moduleBase,
    int dispOffset,
    int instrLength) {

    if (matchAddress == 0 || dispOffset < 0 || instrLength <= dispOffset + 4) {
        return std::nullopt;
    }

    const auto dispBytes = process.readBytes(matchAddress + static_cast<uint64_t>(dispOffset), 4);
    if (dispBytes.size() != 4) {
        return std::nullopt;
    }

    int32_t displacement = 0;
    std::memcpy(&displacement, dispBytes.data(), sizeof(displacement));

    RipResolveResult result;
    result.instructionAddress = matchAddress;
    result.globalAddress = matchAddress + static_cast<uint64_t>(instrLength) + static_cast<uint64_t>(displacement);
    result.rva = result.globalAddress - moduleBase;

    if (const auto ptr = process.read<uint64_t>(result.globalAddress)) {
        result.dereferenced = *ptr;
    }

    return result;
}

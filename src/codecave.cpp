#include "codecave.h"

#include <iostream>

Codecaves::Codecaves(Kernel& kernel)
    : kernel_(kernel) {}

bool Codecaves::allocate(uint32_t pid, uint64_t moduleBase, size_t totalSize) {
    if (isAllocated()) {
        return true;
    }

    pid_ = pid;
    allocationSize_ = totalSize ? totalSize : 0x1000;

    baseAddress_ = 0;
    return false;
}

void Codecaves::deallocate(uint32_t pid) {
    (void)pid;
    baseAddress_ = 0;
    allocationSize_ = 0;
    pid_ = 0;
    configs_.clear();
}

bool Codecaves::patch(uint32_t pid, const CodecaveConfig& config) {
    if (!isAllocated() || !kernel_.isOpen()) {
        return false;
    }

    if (config.patchAddress == 0 || config.patchedBytes.empty()) {
        return false;
    }

    const bool written = kernel_.write(
        pid,
        config.patchAddress,
        config.patchedBytes.data(),
        config.patchedBytes.size());

    if (written) {
        configs_.push_back(config);
    }

    return written;
}

bool Codecaves::restore(uint32_t pid, CodecaveConfig& config) {
    if (!kernel_.isOpen() || config.originalBytes.empty()) {
        return false;
    }

    return kernel_.write(
        pid,
        config.patchAddress,
        config.originalBytes.data(),
        config.originalBytes.size());
}

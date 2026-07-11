#pragma once

#include "kernel.h"
#include "math_util.h"

#include <cstdint>
#include <string>
#include <vector>

struct EntityData {
    uint64_t address = 0;
    Vector3  position;
    float    health = 0.0f;
    int32_t  teamId = 0;
    uint64_t namePtr = 0;
    uint64_t boneComponent = 0;
    bool     isDead = false;
    std::string name;
};

struct GameManagerData {
    uint64_t entityList = 0;
    uint32_t entityCount = 0;
};

class EntityReader {
public:
    EntityReader(Kernel& kernel, uint32_t pid);

    GameManagerData readGameManager(uint64_t gameManagerPtr,
                                     uint32_t entityListOffset,
                                     uint32_t entityCountOffset) const;

    std::vector<EntityData> readEntities(
        uint64_t gameManagerPtr,
        uint32_t entityListOffset,
        uint32_t entityCountOffset,
        uint32_t positionOffset,
        uint32_t healthOffset,
        uint32_t teamIdOffset,
        uint32_t namePtrOffset,
        uint32_t boneComponentOffset) const;

    std::vector<EntityData> readEntities(
        uint64_t entityList,
        uint32_t entityCount,
        uint32_t positionOffset,
        uint32_t healthOffset,
        uint32_t teamIdOffset,
        uint32_t namePtrOffset,
        uint32_t boneComponentOffset) const;

private:
    Kernel& kernel_;
    uint32_t pid_;
};

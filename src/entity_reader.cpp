#include "entity_reader.h"

#include <cstring>

namespace {

bool isReadablePointer(uint64_t ptr) {
    return ptr > 0x10000 && ptr < 0x7FFFFFFFFFFFULL;
}

} // namespace

EntityReader::EntityReader(Kernel& kernel, uint32_t pid)
    : kernel_(kernel), pid_(pid) {}

GameManagerData EntityReader::readGameManager(
    uint64_t gameManagerPtr,
    uint32_t entityListOffset,
    uint32_t entityCountOffset) const {

    GameManagerData data{};

    const auto entityList = kernel_.read<uint64_t>(pid_, gameManagerPtr + entityListOffset);
    if (entityList && isReadablePointer(*entityList)) {
        data.entityList = *entityList;
    }

    const auto entityCount = kernel_.read<uint32_t>(pid_, gameManagerPtr + entityCountOffset);
    if (entityCount) {
        data.entityCount = *entityCount;
    }

    return data;
}

std::vector<EntityData> EntityReader::readEntities(
    uint64_t gameManagerPtr,
    uint32_t entityListOffset,
    uint32_t entityCountOffset,
    uint32_t positionOffset,
    uint32_t healthOffset,
    uint32_t teamIdOffset,
    uint32_t namePtrOffset,
    uint32_t boneComponentOffset) const {

    const auto gm = readGameManager(gameManagerPtr, entityListOffset, entityCountOffset);
    return readEntities(gm.entityList, gm.entityCount,
                        positionOffset, healthOffset,
                        teamIdOffset, namePtrOffset, boneComponentOffset);
}

std::vector<EntityData> EntityReader::readEntities(
    uint64_t entityList,
    uint32_t entityCount,
    uint32_t positionOffset,
    uint32_t healthOffset,
    uint32_t teamIdOffset,
    uint32_t namePtrOffset,
    uint32_t boneComponentOffset) const {

    std::vector<EntityData> entities;

    if (!isReadablePointer(entityList) || entityCount == 0) {
        return entities;
    }

    const uint32_t maxEntities = std::min<uint32_t>(entityCount, 128);

    for (uint32_t i = 0; i < maxEntities; ++i) {
        const auto entityPtr = kernel_.read<uint64_t>(pid_, entityList + static_cast<uint64_t>(i) * 8);
        if (!entityPtr || !isReadablePointer(*entityPtr)) {
            continue;
        }

        EntityData entity;
        entity.address = *entityPtr;

        const auto position = kernel_.readBytes(pid_, *entityPtr + positionOffset, 12);
        if (position.size() == 12) {
            std::memcpy(&entity.position.x, position.data(), 4);
            std::memcpy(&entity.position.y, position.data() + 4, 4);
            std::memcpy(&entity.position.z, position.data() + 8, 4);
        }

        const auto health = kernel_.read<float>(pid_, *entityPtr + healthOffset);
        if (health) {
            entity.health = *health;
            entity.isDead = (*health <= 0.0f);
        }

        const auto team = kernel_.read<int32_t>(pid_, *entityPtr + teamIdOffset);
        if (team) {
            entity.teamId = *team;
        }

        const auto name = kernel_.read<uint64_t>(pid_, *entityPtr + namePtrOffset);
        if (name) {
            entity.namePtr = *name;
        }

        const auto bone = kernel_.read<uint64_t>(pid_, *entityPtr + boneComponentOffset);
        if (bone) {
            entity.boneComponent = *bone;
        }

        entities.push_back(entity);
    }

    return entities;
}

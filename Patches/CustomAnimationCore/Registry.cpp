#include "Registry.h"
#include <mutex>

namespace {
std::mutex registryMutex;
constexpr uint16_t InvalidAnimationId = 0xffff;
constexpr uint16_t FirstDynamicAnimationId = 65000;
}

bool WeaponActionKey::operator==(const WeaponActionKey& other) const {
    return weaponType == other.weaponType && actionKind == other.actionKind;
}

size_t WeaponActionKeyHash::operator()(const WeaponActionKey& key) const {
    return (static_cast<size_t>(key.weaponType) << 8) | key.actionKind;
}

CustomAnimationRegistry& CustomAnimationRegistry::Instance() {
    static CustomAnimationRegistry registry;
    return registry;
}

uint16_t CustomAnimationRegistry::RegisterAnimation(const char* name) {
    if (!name || !*name) {
        return InvalidAnimationId;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    const std::string key(name);
    auto existing = nameToId.find(key);
    if (existing != nameToId.end()) {
        return existing->second;
    }

    while (nextId != InvalidAnimationId && idToName.find(nextId) != idToName.end()) {
        ++nextId;
    }

    if (nextId == InvalidAnimationId) {
        return InvalidAnimationId;
    }

    const uint16_t id = nextId++;
    nameToId.emplace(key, id);
    idToName.emplace(id, key);
    return id;
}

bool CustomAnimationRegistry::RegisterAnimationWithId(const char* name, uint16_t id) {
    if (!name || !*name || id == InvalidAnimationId) {
        return false;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    const std::string key(name);
    auto existing = nameToId.find(key);
    if (existing != nameToId.end()) {
        return existing->second == id;
    }

    auto existingId = idToName.find(id);
    if (existingId != idToName.end() && existingId->second != key) {
        return false;
    }

    nameToId.emplace(key, id);
    idToName.emplace(id, key);
    if (id >= nextId && id < InvalidAnimationId) {
        nextId = static_cast<uint16_t>(id + 1);
    }
    return true;
}

bool CustomAnimationRegistry::MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName) {
    if (!animName || !*animName) {
        return false;
    }

    const uint16_t id = LookupAnimationId(animName);
    if (id == InvalidAnimationId) {
        return false;
    }

    std::lock_guard<std::mutex> lock(registryMutex);
    weaponActionToName[{ weaponType, actionKind }] = animName;
    return true;
}

const char* CustomAnimationRegistry::LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) {
    std::lock_guard<std::mutex> lock(registryMutex);

    const WeaponActionKey candidates[] = {
        { weaponType, actionKind },
        { weaponType, WildcardKey },
        { WildcardKey, actionKind },
        { WildcardKey, WildcardKey },
    };

    for (const WeaponActionKey& candidate : candidates) {
        auto existing = weaponActionToName.find(candidate);
        if (existing != weaponActionToName.end()) {
            return existing->second.c_str();
        }
    }

    return nullptr;
}

uint16_t CustomAnimationRegistry::LookupAnimationId(const char* name) {
    if (!name || !*name) {
        return InvalidAnimationId;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    auto existing = nameToId.find(name);
    if (existing == nameToId.end()) {
        return InvalidAnimationId;
    }
    return existing->second;
}

void CustomAnimationRegistry::Clear() {
    std::lock_guard<std::mutex> lock(registryMutex);
    nameToId.clear();
    idToName.clear();
    weaponActionToName.clear();
    nextId = FirstDynamicAnimationId;
}

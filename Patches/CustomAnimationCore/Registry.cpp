#include "Registry.h"
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>

namespace {
std::mutex registryMutex;
constexpr uint16_t InvalidAnimationId = 0xffff;
constexpr uint16_t FirstDynamicAnimationId = 65000;

std::string NormalizeLookupKey(const char* value) {
    std::string key(value ? value : "");
    std::transform(
        key.begin(),
        key.end(),
        key.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }
    );
    return key;
}

std::string BuildModelPlayOverrideKey(const char* modelName, const char* fromName) {
    return NormalizeLookupKey(modelName) + "\n" + NormalizeLookupKey(fromName);
}
}

bool ResolverKey::operator==(const ResolverKey& other) const {
    return family == other.family && key1 == other.key1 && key2 == other.key2;
}

size_t ResolverKeyHash::operator()(const ResolverKey& key) const {
    return (static_cast<size_t>(key.family) << 16)
        | (static_cast<size_t>(key.key1) << 8)
        | key.key2;
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
    return MapResolverAnimation(
        static_cast<uint8_t>(AnimationResolverFamily::Any),
        weaponType,
        actionKind,
        animName
    );
}

bool CustomAnimationRegistry::MapResolverAnimation(
    uint8_t resolverFamily,
    uint8_t key1,
    uint8_t key2,
    const char* animName
) {
    if (!animName || !*animName) {
        return false;
    }

    const uint16_t id = LookupAnimationId(animName);
    if (id == InvalidAnimationId) {
        return false;
    }

    std::lock_guard<std::mutex> lock(registryMutex);
    resolverToName[{ resolverFamily, key1, key2 }] = animName;
    return true;
}

bool CustomAnimationRegistry::MapAnimationIdOverride(uint16_t fromId, uint16_t toId) {
    if (fromId == InvalidAnimationId || toId == InvalidAnimationId) {
        return false;
    }

    std::lock_guard<std::mutex> lock(registryMutex);
    if (idToName.find(toId) == idToName.end()) {
        return false;
    }

    animationIdOverrides[fromId] = toId;
    return true;
}

bool CustomAnimationRegistry::MapPlayAnimationNameOverride(const char* fromName, const char* toName) {
    if (!fromName || !*fromName || !toName || !*toName) {
        return false;
    }

    std::lock_guard<std::mutex> lock(registryMutex);
    if (nameToId.find(toName) == nameToId.end()) {
        return false;
    }

    playAnimationNameOverrides[fromName] = toName;
    return true;
}

bool CustomAnimationRegistry::MapPlayAnimationNameOverrideForModel(
    const char* modelName,
    const char* fromName,
    const char* toName
) {
    if (!modelName || !*modelName || !fromName || !*fromName || !toName || !*toName) {
        return false;
    }

    std::lock_guard<std::mutex> lock(registryMutex);
    if (nameToId.find(toName) == nameToId.end()) {
        return false;
    }

    playAnimationNameOverridesByModel[BuildModelPlayOverrideKey(modelName, fromName)] = toName;
    return true;
}

const char* CustomAnimationRegistry::LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) {
    return LookupRegisteredResolverAnim(
        static_cast<uint8_t>(AnimationResolverFamily::Any),
        weaponType,
        actionKind
    );
}

const char* CustomAnimationRegistry::LookupRegisteredResolverAnim(
    uint8_t resolverFamily,
    uint8_t key1,
    uint8_t key2
) {
    std::lock_guard<std::mutex> lock(registryMutex);

    const ResolverKey candidates[] = {
        { resolverFamily, key1, key2 },
        { resolverFamily, key1, WildcardKey },
        { resolverFamily, WildcardKey, key2 },
        { resolverFamily, WildcardKey, WildcardKey },
        { static_cast<uint8_t>(AnimationResolverFamily::Any), key1, key2 },
        { static_cast<uint8_t>(AnimationResolverFamily::Any), key1, WildcardKey },
        { static_cast<uint8_t>(AnimationResolverFamily::Any), WildcardKey, key2 },
        { static_cast<uint8_t>(AnimationResolverFamily::Any), WildcardKey, WildcardKey },
    };

    for (const ResolverKey& candidate : candidates) {
        auto existing = resolverToName.find(candidate);
        if (existing != resolverToName.end()) {
            return existing->second.c_str();
        }
    }

    return nullptr;
}

uint16_t CustomAnimationRegistry::LookupAnimationIdOverride(uint16_t fromId) {
    if (fromId == InvalidAnimationId) {
        return InvalidAnimationId;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    auto existing = animationIdOverrides.find(fromId);
    if (existing == animationIdOverrides.end()) {
        return InvalidAnimationId;
    }
    return existing->second;
}

const char* CustomAnimationRegistry::LookupPlayAnimationNameOverride(const char* fromName) {
    if (!fromName || !*fromName) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    auto existing = playAnimationNameOverrides.find(fromName);
    if (existing == playAnimationNameOverrides.end()) {
        return nullptr;
    }
    return existing->second.c_str();
}

const char* CustomAnimationRegistry::LookupPlayAnimationNameOverrideForModel(
    const char* modelName,
    const char* fromName
) {
    if (!fromName || !*fromName) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    if (modelName && *modelName) {
        auto modelSpecific = playAnimationNameOverridesByModel.find(
            BuildModelPlayOverrideKey(modelName, fromName)
        );
        if (modelSpecific != playAnimationNameOverridesByModel.end()) {
            return modelSpecific->second.c_str();
        }
    }

    auto generic = playAnimationNameOverrides.find(fromName);
    if (generic == playAnimationNameOverrides.end()) {
        return nullptr;
    }
    return generic->second.c_str();
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

const char* CustomAnimationRegistry::LookupAnimationNameById(uint16_t id) {
    if (id == InvalidAnimationId) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(registryMutex);

    auto existing = idToName.find(id);
    if (existing == idToName.end()) {
        return nullptr;
    }
    return existing->second.c_str();
}

void CustomAnimationRegistry::Clear() {
    std::lock_guard<std::mutex> lock(registryMutex);
    nameToId.clear();
    idToName.clear();
    resolverToName.clear();
    animationIdOverrides.clear();
    playAnimationNameOverrides.clear();
    playAnimationNameOverridesByModel.clear();
    nextId = FirstDynamicAnimationId;
}

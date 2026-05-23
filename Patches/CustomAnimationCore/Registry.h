#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

enum class AnimationResolverFamily : uint8_t {
    Any = 0,
    Melee = 1,
    Ranged = 2,
};

struct ResolverKey {
    uint8_t family;
    uint8_t key1;
    uint8_t key2;

    bool operator==(const ResolverKey& other) const;
};

struct ResolverKeyHash {
    size_t operator()(const ResolverKey& key) const;
};

class CustomAnimationRegistry {
public:
    static constexpr uint8_t WildcardKey = 0xff;

    static CustomAnimationRegistry& Instance();

    uint16_t RegisterAnimation(const char* name);
    bool RegisterAnimationWithId(const char* name, uint16_t id);
    bool MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName);
    bool MapResolverAnimation(uint8_t resolverFamily, uint8_t key1, uint8_t key2, const char* animName);
    bool MapAnimationIdOverride(uint16_t fromId, uint16_t toId);
    bool MapPlayAnimationNameOverride(const char* fromName, const char* toName);
    const char* LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind);
    const char* LookupRegisteredResolverAnim(uint8_t resolverFamily, uint8_t key1, uint8_t key2);
    uint16_t LookupAnimationIdOverride(uint16_t fromId);
    const char* LookupPlayAnimationNameOverride(const char* fromName);
    uint16_t LookupAnimationId(const char* name);
    const char* LookupAnimationNameById(uint16_t id);
    void Clear();

private:
    CustomAnimationRegistry() = default;

    uint16_t nextId = 65000;
    std::unordered_map<std::string, uint16_t> nameToId;
    std::unordered_map<uint16_t, std::string> idToName;
    std::unordered_map<ResolverKey, std::string, ResolverKeyHash> resolverToName;
    std::unordered_map<uint16_t, uint16_t> animationIdOverrides;
    std::unordered_map<std::string, std::string> playAnimationNameOverrides;
};

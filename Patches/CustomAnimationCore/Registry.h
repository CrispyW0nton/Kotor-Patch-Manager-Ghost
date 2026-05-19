#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct WeaponActionKey {
    uint8_t weaponType;
    uint8_t actionKind;

    bool operator==(const WeaponActionKey& other) const;
};

struct WeaponActionKeyHash {
    size_t operator()(const WeaponActionKey& key) const;
};

class CustomAnimationRegistry {
public:
    static constexpr uint8_t WildcardKey = 0xff;

    static CustomAnimationRegistry& Instance();

    uint16_t RegisterAnimation(const char* name);
    bool RegisterAnimationWithId(const char* name, uint16_t id);
    bool MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName);
    const char* LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind);
    uint16_t LookupAnimationId(const char* name);
    const char* LookupAnimationNameById(uint16_t id);
    void Clear();

private:
    CustomAnimationRegistry() = default;

    uint16_t nextId = 65000;
    std::unordered_map<std::string, uint16_t> nameToId;
    std::unordered_map<uint16_t, std::string> idToName;
    std::unordered_map<WeaponActionKey, std::string, WeaponActionKeyHash> weaponActionToName;
};

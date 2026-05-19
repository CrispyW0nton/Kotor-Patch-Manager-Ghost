#include "Common.h"
#include "GameAPI/GameVersion.h"
#include "Registry.h"

namespace {
constexpr uint16_t InvalidAnimationId = 0xffff;

uint8_t LowByteOrZero(const uint32_t* value) {
    return value ? static_cast<uint8_t>(*value & 0xff) : 0;
}

uint32_t ResolveAnimationOverride(
    const char* source,
    uint32_t vanillaId,
    const uint32_t* key1,
    const uint32_t* key2
) {
    const uint8_t weaponType = LowByteOrZero(key1);
    const uint8_t actionKind = LowByteOrZero(key2);

    const char* animName = CustomAnimationRegistry::Instance().LookupRegisteredAnim(weaponType, actionKind);
    if (!animName) {
        debugLog(
            "[CustomAnimationCore] %s miss (%u,%u); keeping %u\n",
            source,
            weaponType,
            actionKind,
            vanillaId
        );
        return vanillaId;
    }

    const uint16_t mappedId = CustomAnimationRegistry::Instance().LookupAnimationId(animName);
    if (mappedId == InvalidAnimationId) {
        debugLog(
            "[CustomAnimationCore] %s mapping (%u,%u)->%s has no registered id; keeping %u\n",
            source,
            weaponType,
            actionKind,
            animName,
            vanillaId
        );
        return vanillaId;
    }

    debugLog(
        "[CustomAnimationCore] %s override (%u,%u): %u -> %u (%s)\n",
        source,
        weaponType,
        actionKind,
        vanillaId,
        mappedId,
        animName
    );
    return mappedId;
}
}

extern "C" uint32_t __cdecl ResolveMeleeAnimationOverride(
    uint32_t vanillaId,
    const uint32_t* key1,
    const uint32_t* key2
) {
    return ResolveAnimationOverride("melee", vanillaId, key1, key2);
}

extern "C" uint32_t __cdecl ResolveRangedAnimationOverride(
    uint32_t vanillaId,
    const uint32_t* key1,
    const uint32_t* key2
) {
    return ResolveAnimationOverride("ranged", vanillaId, key1, key2);
}

extern "C" __declspec(naked) void __cdecl OverrideMeleeDefaultAnimationId() {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 8]
        push dword ptr [esp + 4]
        call ResolveMeleeAnimationOverride
        add esp, 12
        mov esi, eax
        ret
    }
}

extern "C" __declspec(naked) void __cdecl OverrideRangedDefaultAnimationId() {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 8]
        push dword ptr [esp + 4]
        call ResolveRangedAnimationOverride
        add esp, 12
        mov esi, eax
        ret
    }
}

extern "C" uint16_t __cdecl RegisterAnimation(const char* name) {
    const uint16_t id = CustomAnimationRegistry::Instance().RegisterAnimation(name);
    debugLog("[CustomAnimationCore] RegisterAnimation(%s) -> %u\n", name ? name : "<null>", id);
    return id;
}

extern "C" bool __cdecl RegisterAnimationWithId(const char* name, uint16_t id) {
    const bool success = CustomAnimationRegistry::Instance().RegisterAnimationWithId(name, id);
    debugLog("[CustomAnimationCore] RegisterAnimationWithId(%s, %u) -> %i\n", name ? name : "<null>", id, success);
    return success;
}

extern "C" bool __cdecl MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName) {
    const bool success = CustomAnimationRegistry::Instance().MapWeaponAction(weaponType, actionKind, animName);
    debugLog(
        "[CustomAnimationCore] MapWeaponAction(weapon=%u, action=%u, anim=%s) -> %i\n",
        weaponType,
        actionKind,
        animName ? animName : "<null>",
        success
    );
    return success;
}

extern "C" const char* __cdecl LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) {
    return CustomAnimationRegistry::Instance().LookupRegisteredAnim(weaponType, actionKind);
}

extern "C" uint16_t __cdecl LookupAnimationId(const char* name) {
    return CustomAnimationRegistry::Instance().LookupAnimationId(name);
}

extern "C" void __cdecl ClearCustomAnimationRegistry() {
    CustomAnimationRegistry::Instance().Clear();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        if (!GameVersion::Initialize()) {
            debugLog("[CustomAnimationCore] ERROR: GameVersion::Initialize() failed\n");
            return FALSE;
        }
        debugLog("[CustomAnimationCore] Attached\n");
        break;

    case DLL_PROCESS_DETACH:
        CustomAnimationRegistry::Instance().Clear();
        GameVersion::Reset();
        break;
    }
    return TRUE;
}

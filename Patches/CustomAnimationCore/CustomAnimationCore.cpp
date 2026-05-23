#include "Common.h"
#include "GameAPI/CExoString.h"
#include "GameAPI/GameVersion.h"
#include "Registry.h"

#include <cstring>

namespace {
constexpr uint16_t InvalidAnimationId = 0xffff;

using CExoStringAssignFn = void*(__thiscall*)(void* thisPtr, char* value);
CExoStringAssignFn cExoStringAssign = nullptr;

uint8_t LowByteOrZero(const uint32_t* value) {
    if (!value) {
        return 0;
    }

    __try {
        return static_cast<uint8_t>(*value & 0xff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool EnsureCExoStringAssign() {
    if (cExoStringAssign) {
        return true;
    }

    try {
        cExoStringAssign = reinterpret_cast<CExoStringAssignFn>(
            GameVersion::GetFunctionAddress("CExoString", "operator=")
        );
    }
    catch (const GameVersionException& e) {
        debugLog("[CustomAnimationCore] ERROR: %s\n", e.what());
        return false;
    }

    return cExoStringAssign != nullptr;
}

bool AssignCExoString(void* outputString, const char* value) {
    if (!outputString || !value || !EnsureCExoStringAssign()) {
        return false;
    }

    cExoStringAssign(outputString, const_cast<char*>(value));
    return true;
}

bool IsInterestingAnimationName(const char* name) {
    if (!name) {
        return false;
    }

    __try {
        return std::strstr(name, "dance")
            || std::strstr(name, "victory")
            || std::strstr(name, "kpmwin1")
            || std::strstr(name, "pause")
            || std::strstr(name, "walk")
            || std::strstr(name, "run");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t ResolveAnimationOverride(
    const char* source,
    uint8_t resolverFamily,
    uint32_t vanillaId,
    const uint32_t* key1,
    const uint32_t* key2
) {
    const uint8_t resolverKey1 = LowByteOrZero(key1);
    const uint8_t resolverKey2 = LowByteOrZero(key2);

    const char* animName = CustomAnimationRegistry::Instance().LookupRegisteredResolverAnim(
        resolverFamily,
        resolverKey1,
        resolverKey2
    );
    if (!animName) {
        debugLog(
            "[CustomAnimationCore] %s miss family=%u keys=(%u,%u); keeping %u\n",
            source,
            resolverFamily,
            resolverKey1,
            resolverKey2,
            vanillaId
        );
        return vanillaId;
    }

    const uint16_t mappedId = CustomAnimationRegistry::Instance().LookupAnimationId(animName);
    if (mappedId == InvalidAnimationId) {
        debugLog(
            "[CustomAnimationCore] %s mapping family=%u keys=(%u,%u)->%s has no registered id; keeping %u\n",
            source,
            resolverFamily,
            resolverKey1,
            resolverKey2,
            animName,
            vanillaId
        );
        return vanillaId;
    }

    debugLog(
        "[CustomAnimationCore] %s override family=%u keys=(%u,%u): %u -> %u (%s)\n",
        source,
        resolverFamily,
        resolverKey1,
        resolverKey2,
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
    return ResolveAnimationOverride(
        "melee",
        static_cast<uint8_t>(AnimationResolverFamily::Melee),
        vanillaId,
        key1,
        key2
    );
}

extern "C" uint32_t __cdecl ResolveRangedAnimationOverride(
    uint32_t vanillaId,
    const uint32_t* key1,
    const uint32_t* key2
) {
    return ResolveAnimationOverride(
        "ranged",
        static_cast<uint8_t>(AnimationResolverFamily::Ranged),
        vanillaId,
        key1,
        key2
    );
}

extern "C" uint32_t __cdecl ResolveCustomAnimationNameFromId(
    uint32_t animationId,
    void* outputString
) {
    const char* animName = CustomAnimationRegistry::Instance().LookupAnimationNameById(
        static_cast<uint16_t>(animationId & 0xffff)
    );
    if (!animName) {
        debugLog("[CustomAnimationCore] GetAnimationName miss for id %u\n", animationId);
        return 0;
    }

    if (!AssignCExoString(outputString, animName)) {
        debugLog("[CustomAnimationCore] GetAnimationName failed to assign %s for id %u\n", animName, animationId);
        return 0;
    }

    debugLog("[CustomAnimationCore] GetAnimationName override id %u -> %s\n", animationId, animName);
    return 1;
}

void ResolveSetAnimationIdOverride(const char* context, uint32_t* animationIdSlot) {
    if (!animationIdSlot) {
        return;
    }

    uint16_t vanillaId = InvalidAnimationId;
    __try {
        vanillaId = static_cast<uint16_t>(*animationIdSlot & 0xffff);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    const uint16_t mappedId = CustomAnimationRegistry::Instance().LookupAnimationIdOverride(vanillaId);
    const char* registeredName = CustomAnimationRegistry::Instance().LookupAnimationNameById(vanillaId);

    static LONG requestLogCount = 0;
    const LONG requestLog = InterlockedIncrement(&requestLogCount);
    if (mappedId == InvalidAnimationId || mappedId == vanillaId) {
        if (registeredName) {
            debugLog(
                "[CustomAnimationCore] %s request #%ld slot=%p registered custom id=%u (%s) pass-through\n",
                context,
                requestLog,
                animationIdSlot,
                vanillaId,
                registeredName
            );
        }
        else if (requestLog <= 200) {
            debugLog(
                "[CustomAnimationCore] %s request #%ld slot=%p id=%u no override\n",
                context,
                requestLog,
                animationIdSlot,
                vanillaId
            );
        }
        return;
    }

    __try {
        *animationIdSlot = mappedId;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (requestLog <= 200) {
        const char* mappedName = CustomAnimationRegistry::Instance().LookupAnimationNameById(mappedId);
        debugLog(
            "[CustomAnimationCore] %s request #%ld slot=%p id override %u -> %u (%s)\n",
            context,
            requestLog,
            animationIdSlot,
            vanillaId,
            mappedId,
            mappedName ? mappedName : "<unknown>"
        );
    }
}

extern "C" void __cdecl OverrideSetAnimationId(uint32_t* animationIdSlot) {
    ResolveSetAnimationIdOverride("SetAnimation", animationIdSlot);
}

extern "C" void __cdecl OverrideSetAnimationInternalId(uint32_t* animationIdSlot) {
    ResolveSetAnimationIdOverride("SetAnimationInternal", animationIdSlot);
}

extern "C" void __cdecl LogPlayAnimationRequest(void* gob, const char** animNameSlot) {
    static LONG requestLogCount = 0;
    const LONG requestLog = InterlockedIncrement(&requestLogCount);

    const char* animName = nullptr;
    __try {
        animName = animNameSlot ? *animNameSlot : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        animName = nullptr;
    }

    if (requestLog > 120 && !IsInterestingAnimationName(animName)) {
        return;
    }

    __try {
        debugLog(
            "[CustomAnimationCore] Gob::PlayAnimation request #%ld gob=%p name=%s\n",
            requestLog,
            gob,
            animName ? animName : "<null>"
        );
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        debugLog(
            "[CustomAnimationCore] Gob::PlayAnimation request #%ld gob=%p name=<invalid:%p>\n",
            requestLog,
            gob,
            animNameSlot
        );
    }
}

void LogAnimationPlayCall(const char* context, void* target, const char* animName) {
    static LONG playCallLogCount = 0;
    const LONG requestLog = InterlockedIncrement(&playCallLogCount);

    if (requestLog > 200 && !IsInterestingAnimationName(animName)) {
        return;
    }

    __try {
        debugLog(
            "[CustomAnimationCore] %s PlayAnimation call #%ld target=%p name=%s\n",
            context,
            requestLog,
            target,
            animName ? animName : "<null>"
        );
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        debugLog(
            "[CustomAnimationCore] %s PlayAnimation call #%ld target=%p name=<invalid:%p>\n",
            context,
            requestLog,
            target,
            animName
        );
    }
}

extern "C" void __cdecl LogBaseAnimationPlayCall(void* target, const char* animName) {
    LogAnimationPlayCall("Base", target, animName);
}

extern "C" void __cdecl LogTWPrimaryAnimationPlayCall(void* target, const char* animName) {
    LogAnimationPlayCall("TWPrimary", target, animName);
}

extern "C" void __cdecl LogTWSecondaryAnimationPlayCall(void* target, const char* animName) {
    LogAnimationPlayCall("TWSecondary", target, animName);
}

extern "C" __declspec(naked) void __cdecl OverrideMeleeDefaultAnimationId() {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 8]
        push dword ptr [esp + 4]
        call ResolveMeleeAnimationOverride
        add esp, 12

        // KPM's wrapper saved the original game ESI at [EBX+8]. Patch that
        // saved value, restore the wrapper state, then run the original game
        // epilogue ourselves. Returning to the wrapper would make it replay a
        // RET from wrapper memory, not from UpdateMeleeAttackData's frame.
        mov dword ptr [ebx + 8], eax
        mov edx, ebx
        mov esp, edx
        popfd
        popad

        pop edi
        mov eax, esi
        pop esi
        pop ebx
        ret 0x10
    }
}

extern "C" __declspec(naked) void __cdecl OverrideRangedDefaultAnimationId() {
    __asm {
        push dword ptr [esp + 12]
        push dword ptr [esp + 8]
        push dword ptr [esp + 4]
        call ResolveRangedAnimationOverride
        add esp, 12

        // See OverrideMeleeDefaultAnimationId for why this hook restores state
        // and emulates the original epilogue instead of returning to the wrapper.
        mov dword ptr [ebx + 8], eax
        mov edx, ebx
        mov esp, edx
        popfd
        popad

        mov eax, esi
        pop esi
        ret 0x08
    }
}

extern "C" __declspec(naked) void __cdecl OverrideAnimationNameLookup() {
    __asm {
        // Check the registry before honoring the vanilla animations.2da result.
        // This lets a smoke test keep a vanilla row's timing/metadata while
        // swapping only the model animation name that row resolves to.
        mov ecx, dword ptr [ebx + 16]
        lea ecx, [ecx + 0x18]
        push ecx
        push edi
        call ResolveCustomAnimationNameFromId
        add esp, 8
        test eax, eax
        jnz CustomAnimationNameSuccess

        mov eax, dword ptr [ebx + 32]
        test eax, eax
        jnz CustomAnimationNameSuccess

    CustomAnimationNameFailure:
        mov edx, ebx
        mov esp, edx
        popfd
        popad
        push 0x0073d71c
        mov eax, 0x0069e699
        jmp eax

    CustomAnimationNameSuccess:
        mov edx, ebx
        mov esp, edx
        popfd
        popad
        mov eax, 0x0069e6a2
        jmp eax
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

extern "C" bool __cdecl MapResolverAnimation(
    uint8_t resolverFamily,
    uint8_t key1,
    uint8_t key2,
    const char* animName
) {
    const bool success = CustomAnimationRegistry::Instance().MapResolverAnimation(
        resolverFamily,
        key1,
        key2,
        animName
    );
    debugLog(
        "[CustomAnimationCore] MapResolverAnimation(family=%u, key1=%u, key2=%u, anim=%s) -> %i\n",
        resolverFamily,
        key1,
        key2,
        animName ? animName : "<null>",
        success
    );
    return success;
}

extern "C" bool __cdecl MapAnimationIdOverride(uint16_t fromId, uint16_t toId) {
    const bool success = CustomAnimationRegistry::Instance().MapAnimationIdOverride(fromId, toId);
    debugLog("[CustomAnimationCore] MapAnimationIdOverride(%u -> %u) -> %i\n", fromId, toId, success);
    return success;
}

extern "C" const char* __cdecl LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) {
    return CustomAnimationRegistry::Instance().LookupRegisteredAnim(weaponType, actionKind);
}

extern "C" const char* __cdecl LookupRegisteredResolverAnim(uint8_t resolverFamily, uint8_t key1, uint8_t key2) {
    return CustomAnimationRegistry::Instance().LookupRegisteredResolverAnim(resolverFamily, key1, key2);
}

extern "C" uint16_t __cdecl LookupAnimationId(const char* name) {
    return CustomAnimationRegistry::Instance().LookupAnimationId(name);
}

extern "C" const char* __cdecl LookupAnimationNameById(uint16_t id) {
    return CustomAnimationRegistry::Instance().LookupAnimationNameById(id);
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

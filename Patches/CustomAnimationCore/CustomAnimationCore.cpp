#include "Common.h"
#include "GameAPI/CExoString.h"
#include "GameAPI/GameVersion.h"
#include "Registry.h"

#include <cstdint>
#include <cstring>
#include <string.h>

namespace {
constexpr uint16_t InvalidAnimationId = 0xffff;
constexpr size_t LastAnimationNameCapacity = 64;

using CExoStringAssignFn = void*(__thiscall*)(void* thisPtr, char* value);
CExoStringAssignFn cExoStringAssign = nullptr;

struct LastAnimationExistsProbe {
    LONG sequence = 0;
    void* animBase = nullptr;
    void* target = nullptr;
    uint16_t animationId = InvalidAnimationId;
    char name[LastAnimationNameCapacity] = {};
};

LastAnimationExistsProbe lastAnimationExistsProbe;

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

void CopyProbeName(char* destination, const char* source) {
    if (!destination) {
        return;
    }

    destination[0] = '\0';
    if (!source) {
        return;
    }

    __try {
        std::strncpy(destination, source, LastAnimationNameCapacity - 1);
        destination[LastAnimationNameCapacity - 1] = '\0';
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        destination[0] = '\0';
    }
}

const char* SafeReadCString(const char* value) {
    if (!value) {
        return nullptr;
    }

    __try {
        volatile char first = value[0];
        (void)first;
        return value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* SafeReadPointer(void* base, size_t offset) {
    if (!base) {
        return nullptr;
    }

    __try {
        return *reinterpret_cast<void**>(static_cast<uint8_t*>(base) + offset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int32_t SafeReadInt32(void* base, size_t offset) {
    if (!base) {
        return 0;
    }

    __try {
        return *reinterpret_cast<int32_t*>(static_cast<uint8_t*>(base) + offset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

float SafeReadFloat(void* base, size_t offset) {
    if (!base) {
        return 0.0f;
    }

    __try {
        return *reinterpret_cast<float*>(static_cast<uint8_t*>(base) + offset);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
}

const char* SafeReadAnimationName(void* animation) {
    if (!animation) {
        return nullptr;
    }

    return SafeReadCString(reinterpret_cast<const char*>(static_cast<uint8_t*>(animation) + 0x8));
}

void* FindAnimationInModelChain(void* model, const char* animName, int depth = 0) {
    if (!model || !animName || depth > 16) {
        return nullptr;
    }

    const int32_t localAnimCount = SafeReadInt32(model, 0x5c);
    void* localAnimArray = SafeReadPointer(model, 0x58);
    if (localAnimCount > 0 && localAnimCount < 4096 && localAnimArray) {
        for (int32_t index = 0; index < localAnimCount; ++index) {
            void* animation = nullptr;
            __try {
                animation = *(reinterpret_cast<void**>(localAnimArray) + index);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                animation = nullptr;
            }

            const char* candidateName = SafeReadAnimationName(animation);
            if (candidateName && _stricmp(candidateName, animName) == 0) {
                return animation;
            }
        }
    }

    return FindAnimationInModelChain(SafeReadPointer(model, 0x64), animName, depth + 1);
}

uintptr_t SafeReadReturnAddress(const char** stackSlot) {
    if (!stackSlot) {
        return 0;
    }

    __try {
        return reinterpret_cast<uintptr_t>(*(stackSlot - 1));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* ResolveRegisteredNameForId(uint16_t requestedId, uint16_t* resolvedIdOut = nullptr) {
    uint16_t resolvedId = requestedId;
    const char* animName = CustomAnimationRegistry::Instance().LookupAnimationNameById(requestedId);
    if (!animName) {
        const uint16_t mappedId = CustomAnimationRegistry::Instance().LookupAnimationIdOverride(requestedId);
        if (mappedId != InvalidAnimationId) {
            resolvedId = mappedId;
            animName = CustomAnimationRegistry::Instance().LookupAnimationNameById(mappedId);
        }
    }

    if (resolvedIdOut) {
        *resolvedIdOut = resolvedId;
    }
    return animName;
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
    const uint16_t requestedId = static_cast<uint16_t>(animationId & 0xffff);
    uint16_t resolvedId = requestedId;
    const char* animName = CustomAnimationRegistry::Instance().LookupAnimationNameById(requestedId);
    if (!animName) {
        const uint16_t mappedId = CustomAnimationRegistry::Instance().LookupAnimationIdOverride(requestedId);
        if (mappedId != InvalidAnimationId) {
            resolvedId = mappedId;
            animName = CustomAnimationRegistry::Instance().LookupAnimationNameById(mappedId);
        }
    }

    if (!animName) {
        debugLog("[CustomAnimationCore] GetAnimationName miss for id %u\n", animationId);
        return 0;
    }

    if (!AssignCExoString(outputString, animName)) {
        debugLog("[CustomAnimationCore] GetAnimationName failed to assign %s for id %u\n", animName, animationId);
        return 0;
    }

    if (resolvedId != requestedId) {
        debugLog(
            "[CustomAnimationCore] GetAnimationName override id %u -> %u (%s)\n",
            animationId,
            resolvedId,
            animName
        );
    }
    else {
        debugLog("[CustomAnimationCore] GetAnimationName override id %u -> %s\n", animationId, animName);
    }
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

    const char* rawName = nullptr;
    __try {
        rawName = animNameSlot ? *animNameSlot : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        rawName = nullptr;
    }

    rawName = SafeReadCString(rawName);
    const uintptr_t caller = SafeReadReturnAddress(animNameSlot);
    const char* plannedOverride = CustomAnimationRegistry::Instance().LookupPlayAnimationNameOverride(rawName);

    if (requestLog <= 200 || IsInterestingAnimationName(rawName)) {
        debugLog(
            "[CustomAnimationCore] Gob::PlayAnimation ENTRY #%ld gob=%p caller=%p raw_name=%s planned=%s slot=%p\n",
            requestLog,
            gob,
            reinterpret_cast<void*>(caller),
            rawName ? rawName : "<null>",
            plannedOverride ? plannedOverride : "<none>",
            animNameSlot
        );
    }

    if (requestLog > 120 && !IsInterestingAnimationName(rawName) && !plannedOverride) {
        return;
    }

    __try {
        debugLog(
            "[CustomAnimationCore] Gob::PlayAnimation ENTRY_DONE #%ld gob=%p raw_name=%s caller=%p\n",
            requestLog,
            gob,
            rawName ? rawName : "<null>",
            reinterpret_cast<void*>(caller)
        );
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        debugLog(
            "[CustomAnimationCore] Gob::PlayAnimation ENTRY_DONE #%ld gob=%p raw_name=<invalid:%p>\n",
            requestLog,
            gob,
            animNameSlot
        );
    }
}

extern "C" const char* __cdecl ResolvePlayAnimationNameRegisterOverride(const char* rawName, void* gob) {
    static LONG registerLogCount = 0;
    const LONG registerLog = InterlockedIncrement(&registerLogCount);
    const char* safeName = SafeReadCString(rawName);
    const char* overrideName = CustomAnimationRegistry::Instance().LookupPlayAnimationNameOverride(safeName);
    if (!overrideName) {
        if (registerLog <= 200 || IsInterestingAnimationName(safeName)) {
            debugLog(
                "[CustomAnimationCore] Gob::PlayAnimation REGISTER_KEEP #%ld gob=%p name=%s\n",
                registerLog,
                gob,
                safeName ? safeName : "<null>"
            );
        }
        return rawName;
    }

    void* localModel = SafeReadPointer(gob, 0x58);
    void* addInModel = SafeReadPointer(gob, 0x64);
    void* localAnimation = FindAnimationInModelChain(localModel, overrideName);
    void* addInAnimation = localAnimation ? nullptr : FindAnimationInModelChain(addInModel, overrideName);
    void* resolvedAnimation = localAnimation ? localAnimation : addInAnimation;
    const char* localModelName = SafeReadAnimationName(localModel);
    const char* addInModelName = SafeReadAnimationName(addInModel);
    if (!resolvedAnimation) {
        debugLog(
            "[CustomAnimationCore] Gob::PlayAnimation REGISTER_UNAVAILABLE #%ld gob=%p original=%s requested=%s local_model=%s addin_model=%s; keeping original\n",
            registerLog,
            gob,
            safeName ? safeName : "<null>",
            overrideName,
            localModelName ? localModelName : "<null>",
            addInModelName ? addInModelName : "<null>"
        );
        return rawName;
    }

    debugLog(
        "[CustomAnimationCore] Gob::PlayAnimation REGISTER_MAPPED #%ld gob=%p original=%s resolved=%s source=%s animation=%p local_model=%s addin_model=%s\n",
        registerLog,
        gob,
        safeName ? safeName : "<null>",
        overrideName,
        localAnimation ? "local" : "addin",
        resolvedAnimation,
        localModelName ? localModelName : "<null>",
        addInModelName ? addInModelName : "<null>"
    );
    return overrideName;
}

extern "C" __declspec(naked) void __cdecl OverridePlayAnimationNameRegister() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call ResolvePlayAnimationNameRegisterOverride
        add esp, 8

        // KPM's wrapper keeps the saved game registers behind EBX. EBX is the
        // live animation-name register at this hook site, so patch the saved
        // EBX slot before restoring CPU state.
        mov dword ptr [ebx + 20], eax
        mov edx, ebx
        mov esp, edx
        popfd
        popad

        push ebx
        push 0x0073ee04
        mov eax, 0x00485bfc
        jmp eax
    }
}

extern "C" void __cdecl LogAnimationExistsRequest(void* animBase, uint32_t* animationIdSlot) {
    static LONG requestLogCount = 0;
    const LONG requestLog = InterlockedIncrement(&requestLogCount);

    uint16_t requestedId = InvalidAnimationId;
    __try {
        requestedId = animationIdSlot ? static_cast<uint16_t>(*animationIdSlot & 0xffff) : InvalidAnimationId;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        requestedId = InvalidAnimationId;
    }

    uint16_t resolvedId = requestedId;
    const char* registeredName = ResolveRegisteredNameForId(requestedId, &resolvedId);
    if (requestLog <= 200 || IsInterestingAnimationName(registeredName)) {
        if (registeredName && resolvedId != requestedId) {
            debugLog(
                "[CustomAnimationCore] AnimationExists ENTRY #%ld animBase=%p id=%u mapped=%u name=%s\n",
                requestLog,
                animBase,
                requestedId,
                resolvedId,
                registeredName
            );
        }
        else {
            debugLog(
                "[CustomAnimationCore] AnimationExists ENTRY #%ld animBase=%p id=%u name=%s\n",
                requestLog,
                animBase,
                requestedId,
                registeredName ? registeredName : "<vanilla-or-unresolved>"
            );
        }
    }
}

extern "C" void __cdecl LogAnimationExistsLookup(void* target, const char* animName) {
    static LONG lookupLogCount = 0;
    const LONG lookupLog = InterlockedIncrement(&lookupLogCount);
    const char* safeName = SafeReadCString(animName);
    const bool interesting = IsInterestingAnimationName(safeName);

    if (lookupLog <= 200 || interesting) {
        debugLog(
            "[CustomAnimationCore] AnimationExists LOOKUP #%ld target=%p name=%s\n",
            lookupLog,
            target,
            safeName ? safeName : "<null>"
        );
    }

    lastAnimationExistsProbe.sequence = lookupLog;
    lastAnimationExistsProbe.target = target;
    CopyProbeName(lastAnimationExistsProbe.name, safeName);
}

void LogAnimationExistsResult(const char* resultText, int resultValue) {
    if (lastAnimationExistsProbe.sequence <= 0 && resultValue == 0) {
        return;
    }

    if (lastAnimationExistsProbe.sequence <= 200 || IsInterestingAnimationName(lastAnimationExistsProbe.name)) {
        debugLog(
            "[CustomAnimationCore] AnimationExists RESULT #%ld target=%p name=%s result=%s\n",
            lastAnimationExistsProbe.sequence,
            lastAnimationExistsProbe.target,
            lastAnimationExistsProbe.name[0] ? lastAnimationExistsProbe.name : "<unknown>",
            resultText
        );
    }
}

extern "C" void __cdecl LogAnimationExistsResultTrue() {
    LogAnimationExistsResult("TRUE", 1);
}

extern "C" void __cdecl LogAnimationExistsResultFalse() {
    LogAnimationExistsResult("FALSE", 0);
}

extern "C" void __cdecl LogAnimRunConstruct(void* animRun, uint32_t* animationSlot) {
    static LONG constructLogCount = 0;
    const LONG constructLog = InterlockedIncrement(&constructLogCount);

    void* animation = nullptr;
    __try {
        animation = animationSlot ? reinterpret_cast<void*>(*animationSlot) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        animation = nullptr;
    }

    const char* animName = SafeReadAnimationName(animation);
    const float duration = SafeReadFloat(animation, 0x50);

    if (constructLog <= 200 || IsInterestingAnimationName(animName)) {
        debugLog(
            "[CustomAnimationCore] AnimRun CREATE #%ld run=%p animation=%p name=%s duration=%.3f\n",
            constructLog,
            animRun,
            animation,
            animName ? animName : "<unknown>",
            duration
        );
    }
}

bool ShouldLogFindAnimationProbe(LONG sequence, const char* animName) {
    return sequence <= 500
        || IsInterestingAnimationName(animName)
        || CustomAnimationRegistry::Instance().LookupPlayAnimationNameOverride(animName) != nullptr;
}

extern "C" void __cdecl LogFindAnimationSearch(void* model, const char* animName) {
    static LONG searchLogCount = 0;
    const LONG searchLog = InterlockedIncrement(&searchLogCount);
    const char* safeName = SafeReadCString(animName);
    if (!ShouldLogFindAnimationProbe(searchLog, safeName)) {
        return;
    }

    const char* modelName = SafeReadAnimationName(model);
    const int32_t localAnimCount = SafeReadInt32(model, 0x5c);
    void* localAnimArray = SafeReadPointer(model, 0x58);
    void* superModel = SafeReadPointer(model, 0x64);

    debugLog(
        "[CustomAnimationCore] FindAnimation SEARCH #%ld model=%p model_name=%s name=%s local_count=%d local_array=%p super=%p\n",
        searchLog,
        model,
        modelName ? modelName : "<unknown>",
        safeName ? safeName : "<null>",
        localAnimCount,
        localAnimArray,
        superModel
    );
}

void LogGobFindAnimationResult(
    const char* context,
    void* gob,
    const char* animName,
    void* result,
    size_t modelOffset
) {
    static LONG resultLogCount = 0;
    const LONG resultLog = InterlockedIncrement(&resultLogCount);
    const char* safeName = SafeReadCString(animName);
    if (!ShouldLogFindAnimationProbe(resultLog, safeName)) {
        return;
    }

    void* model = SafeReadPointer(gob, modelOffset);
    const char* modelName = SafeReadAnimationName(model);
    const char* resultName = SafeReadAnimationName(result);
    const float resultDuration = SafeReadFloat(result, 0x50);

    debugLog(
        "[CustomAnimationCore] FindAnimation %s RESULT #%ld gob=%p model=%p model_name=%s query=%s result=%p result_name=%s duration=%.3f\n",
        context,
        resultLog,
        gob,
        model,
        modelName ? modelName : "<unknown>",
        safeName ? safeName : "<null>",
        result,
        resultName ? resultName : "<null>",
        resultDuration
    );
}

extern "C" void __cdecl LogGobFindAnimationAddInResult(void* gob, const char* animName, void* result) {
    LogGobFindAnimationResult("ADDIN", gob, animName, result, 0x64);
}

extern "C" void __cdecl LogGobFindAnimationLocalResult(void* gob, const char* animName, void* result) {
    LogGobFindAnimationResult("LOCAL", gob, animName, result, 0x58);
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

extern "C" bool __cdecl MapPlayAnimationNameOverride(const char* fromName, const char* toName) {
    const bool success = CustomAnimationRegistry::Instance().MapPlayAnimationNameOverride(fromName, toName);
    debugLog(
        "[CustomAnimationCore] MapPlayAnimationNameOverride(%s -> %s) -> %i\n",
        fromName ? fromName : "<null>",
        toName ? toName : "<null>",
        success
    );
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

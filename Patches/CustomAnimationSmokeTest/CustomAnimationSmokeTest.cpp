#include "Common.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr uint8_t WildcardKey = 0xff;
constexpr uint8_t ResolverFamilyAny = 0;
constexpr uint16_t SmokeAnimationId = 10000;
constexpr uint16_t InvalidAnimationId = 0xffff;
constexpr const char* SmokeAnimationName = "kpmwin1";
constexpr bool EnableWildcardResolverProof = false;
constexpr const char* SmokePlayNameOverrides[] = {
    "default",
    "pause1",
};

using RegisterAnimationWithIdFn = bool(__cdecl*)(const char*, uint16_t);
using RegisterAnimationFn = uint16_t(__cdecl*)(const char*);
using MapResolverAnimationFn = bool(__cdecl*)(uint8_t, uint8_t, uint8_t, const char*);
using MapPlayAnimationNameOverrideForModelFn = bool(__cdecl*)(const char*, const char*, const char*);
using LookupAnimationIdFn = uint16_t(__cdecl*)(const char*);
using LookupAnimationNameByIdFn = const char*(__cdecl*)(uint16_t);

constexpr const char* SmokeTargetModels[] = {
    "PMBAL",
    "PMBAM",
};

template <typename T>
T ResolveExport(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

bool InstallSmokeMapping() {
    HMODULE core = GetModuleHandleA("custom-animation-core.dll");
    if (!core) {
        debugLog("[CustomAnimationSmokeTest] ERROR: custom-animation-core.dll is not loaded\n");
        return false;
    }

    auto registerAnimationWithId = ResolveExport<RegisterAnimationWithIdFn>(core, "RegisterAnimationWithId");
    auto registerAnimation = ResolveExport<RegisterAnimationFn>(core, "RegisterAnimation");
    auto mapResolverAnimation = ResolveExport<MapResolverAnimationFn>(core, "MapResolverAnimation");
    auto mapPlayAnimationNameOverrideForModel = ResolveExport<MapPlayAnimationNameOverrideForModelFn>(
        core,
        "MapPlayAnimationNameOverrideForModel"
    );
    auto lookupAnimationId = ResolveExport<LookupAnimationIdFn>(core, "LookupAnimationId");
    auto lookupAnimationNameById = ResolveExport<LookupAnimationNameByIdFn>(core, "LookupAnimationNameById");

    if (!registerAnimationWithId || !registerAnimation || !mapResolverAnimation || !mapPlayAnimationNameOverrideForModel || !lookupAnimationId || !lookupAnimationNameById) {
        debugLog("[CustomAnimationSmokeTest] ERROR: required CustomAnimationCore export is missing\n");
        return false;
    }

    uint16_t registeredId = InvalidAnimationId;
    if (EnableWildcardResolverProof) {
        if (!registerAnimationWithId(SmokeAnimationName, SmokeAnimationId)) {
            debugLog(
                "[CustomAnimationSmokeTest] ERROR: failed to register %s as id %u\n",
                SmokeAnimationName,
                SmokeAnimationId
            );
            return false;
        }
        registeredId = SmokeAnimationId;
    }
    else {
        registeredId = registerAnimation(SmokeAnimationName);
    }

    if (registeredId == InvalidAnimationId) {
        debugLog(
            "[CustomAnimationSmokeTest] ERROR: failed to register %s for direct-name proof\n",
            SmokeAnimationName,
            registeredId
        );
        return false;
    }

    const uint16_t resolvedRow = lookupAnimationId(SmokeAnimationName);
    if (resolvedRow != registeredId) {
        debugLog(
            "[CustomAnimationSmokeTest] ERROR: %s resolved to %u, expected %u\n",
            SmokeAnimationName,
            resolvedRow,
            registeredId
        );
        return false;
    }

    const char* resolvedName = lookupAnimationNameById(registeredId);
    if (!resolvedName || strcmp(resolvedName, SmokeAnimationName) != 0) {
        debugLog(
            "[CustomAnimationSmokeTest] ERROR: id %u resolved to %s, expected %s\n",
            registeredId,
            resolvedName ? resolvedName : "<null>",
            SmokeAnimationName
        );
        return false;
    }

    if (EnableWildcardResolverProof) {
        if (!mapResolverAnimation(ResolverFamilyAny, WildcardKey, WildcardKey, SmokeAnimationName)) {
            debugLog("[CustomAnimationSmokeTest] ERROR: failed to install wildcard mapping\n");
            return false;
        }
        debugLog(
            "[CustomAnimationSmokeTest] Wildcard custom-ID resolver proof enabled -> %s\n",
            SmokeAnimationName
        );
    }
    else {
        debugLog(
            "[CustomAnimationSmokeTest] Wildcard custom-ID resolver proof disabled for direct-name model availability test\n"
        );
    }

    for (const char* fromName : SmokePlayNameOverrides) {
        for (const char* modelName : SmokeTargetModels) {
            if (!mapPlayAnimationNameOverrideForModel(modelName, fromName, SmokeAnimationName)) {
                debugLog(
                    "[CustomAnimationSmokeTest] ERROR: failed to install direct play-name proof mapping %s:%s -> %s\n",
                    modelName,
                    fromName,
                    SmokeAnimationName
                );
                return false;
            }
            debugLog(
                "[CustomAnimationSmokeTest] Direct play-name proof mapping %s:%s -> %s\n",
                modelName,
                fromName,
                SmokeAnimationName
            );
        }
    }

    debugLog(
        "[CustomAnimationSmokeTest] Registered direct-name proof mapping -> id %u resolves to %s\n",
        registeredId,
        SmokeAnimationName
    );
    debugLog(
        "[CustomAnimationSmokeTest] Direct Gob::PlayAnimation requests should resolve to %s\n",
        SmokeAnimationName
    );
    return true;
}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        InstallSmokeMapping();
        break;
    }
    return TRUE;
}

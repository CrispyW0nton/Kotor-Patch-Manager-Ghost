#include "Common.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr uint8_t WildcardKey = 0xff;
constexpr uint16_t SmokeAnimationId = 65000;
constexpr const char* SmokeAnimationName = "victory";

using RegisterAnimationWithIdFn = bool(__cdecl*)(const char*, uint16_t);
using MapWeaponActionFn = bool(__cdecl*)(uint8_t, uint8_t, const char*);
using LookupAnimationIdFn = uint16_t(__cdecl*)(const char*);
using LookupAnimationNameByIdFn = const char*(__cdecl*)(uint16_t);

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
    auto mapWeaponAction = ResolveExport<MapWeaponActionFn>(core, "MapWeaponAction");
    auto lookupAnimationId = ResolveExport<LookupAnimationIdFn>(core, "LookupAnimationId");
    auto lookupAnimationNameById = ResolveExport<LookupAnimationNameByIdFn>(core, "LookupAnimationNameById");

    if (!registerAnimationWithId || !mapWeaponAction || !lookupAnimationId || !lookupAnimationNameById) {
        debugLog("[CustomAnimationSmokeTest] ERROR: required CustomAnimationCore export is missing\n");
        return false;
    }

    if (!registerAnimationWithId(SmokeAnimationName, SmokeAnimationId)) {
        debugLog(
            "[CustomAnimationSmokeTest] ERROR: failed to register %s as id %u\n",
            SmokeAnimationName,
            SmokeAnimationId
        );
        return false;
    }

    const uint16_t resolvedRow = lookupAnimationId(SmokeAnimationName);
    if (resolvedRow != SmokeAnimationId) {
        debugLog(
            "[CustomAnimationSmokeTest] ERROR: %s resolved to %u, expected %u\n",
            SmokeAnimationName,
            resolvedRow,
            SmokeAnimationId
        );
        return false;
    }

    const char* resolvedName = lookupAnimationNameById(SmokeAnimationId);
    if (!resolvedName || strcmp(resolvedName, SmokeAnimationName) != 0) {
        debugLog(
            "[CustomAnimationSmokeTest] ERROR: id %u resolved to %s, expected %s\n",
            SmokeAnimationId,
            resolvedName ? resolvedName : "<null>",
            SmokeAnimationName
        );
        return false;
    }

    if (!mapWeaponAction(WildcardKey, WildcardKey, SmokeAnimationName)) {
        debugLog("[CustomAnimationSmokeTest] ERROR: failed to install wildcard mapping\n");
        return false;
    }

    debugLog(
        "[CustomAnimationSmokeTest] Installed wildcard mapping (*,*) -> %s / id %u\n",
        SmokeAnimationName,
        SmokeAnimationId
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

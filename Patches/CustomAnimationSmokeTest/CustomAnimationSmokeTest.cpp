#include "Common.h"

#include <cstdint>
#include <cstring>

namespace {
constexpr uint8_t WildcardKey = 0xff;
constexpr uint8_t ResolverFamilyAny = 0;
constexpr uint16_t SmokeAnimationId = 65000;
constexpr const char* SmokeAnimationName = "victory";

using RegisterAnimationWithIdFn = bool(__cdecl*)(const char*, uint16_t);
using MapResolverAnimationFn = bool(__cdecl*)(uint8_t, uint8_t, uint8_t, const char*);
using MapAnimationIdOverrideFn = bool(__cdecl*)(uint16_t, uint16_t);
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
    auto mapResolverAnimation = ResolveExport<MapResolverAnimationFn>(core, "MapResolverAnimation");
    auto mapAnimationIdOverride = ResolveExport<MapAnimationIdOverrideFn>(core, "MapAnimationIdOverride");
    auto lookupAnimationId = ResolveExport<LookupAnimationIdFn>(core, "LookupAnimationId");
    auto lookupAnimationNameById = ResolveExport<LookupAnimationNameByIdFn>(core, "LookupAnimationNameById");

    if (!registerAnimationWithId || !mapResolverAnimation || !mapAnimationIdOverride || !lookupAnimationId || !lookupAnimationNameById) {
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

    if (!mapResolverAnimation(ResolverFamilyAny, WildcardKey, WildcardKey, SmokeAnimationName)) {
        debugLog("[CustomAnimationSmokeTest] ERROR: failed to install wildcard mapping\n");
        return false;
    }

    const uint16_t pauseAnimationIds[] = { 6, 7, 8, 9, 12, 13, 14, 15 };
    for (uint16_t pauseId : pauseAnimationIds) {
        if (!mapAnimationIdOverride(pauseId, SmokeAnimationId)) {
            debugLog(
                "[CustomAnimationSmokeTest] ERROR: failed to map pause/idle id %u to %s/%u\n",
                pauseId,
                SmokeAnimationName,
                SmokeAnimationId
            );
            return false;
        }
    }

    debugLog(
        "[CustomAnimationSmokeTest] Installed wildcard combat mapping and pause/idle remaps -> %s / id %u\n",
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

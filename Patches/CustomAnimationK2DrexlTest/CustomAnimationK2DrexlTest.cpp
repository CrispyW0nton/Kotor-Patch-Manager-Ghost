#include "Common.h"

#include <cstdint>

namespace {
constexpr uint16_t DrexlAttackAnimationId = 65000;
constexpr const char* DrexlModelName = "C_DrexlF";
constexpr const char* DrexlAttackAnimationName = "kpm_drx_a1";

constexpr const char* TriggerAnimationNames[] = {
    "default",
    "cpause1",
    "pause2",
    "g0a1",
    "g0a2",
};

using RegisterAnimationWithIdFn = bool(__cdecl*)(const char*, uint16_t);
using MapPlayAnimationNameOverrideForModelFn = bool(__cdecl*)(const char*, const char*, const char*);

template <typename T>
T ResolveExport(HMODULE module, const char* name) {
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

bool InstallDrexlProbe() {
    HMODULE core = GetModuleHandleA("custom-animation-core.dll");
    if (!core) {
        debugLog("[CustomAnimationK2DrexlTest] ERROR: custom-animation-core.dll is not loaded\n");
        return false;
    }

    const auto registerAnimationWithId = ResolveExport<RegisterAnimationWithIdFn>(
        core,
        "RegisterAnimationWithId"
    );
    const auto mapPlayAnimationNameOverrideForModel =
        ResolveExport<MapPlayAnimationNameOverrideForModelFn>(
            core,
            "MapPlayAnimationNameOverrideForModel"
        );

    if (!registerAnimationWithId || !mapPlayAnimationNameOverrideForModel) {
        debugLog("[CustomAnimationK2DrexlTest] ERROR: required core exports are missing\n");
        return false;
    }

    if (!registerAnimationWithId(DrexlAttackAnimationName, DrexlAttackAnimationId)) {
        debugLog(
            "[CustomAnimationK2DrexlTest] ERROR: could not register %s as id %u\n",
            DrexlAttackAnimationName,
            DrexlAttackAnimationId
        );
        return false;
    }

    for (const char* triggerName : TriggerAnimationNames) {
        if (!mapPlayAnimationNameOverrideForModel(
                DrexlModelName,
                triggerName,
                DrexlAttackAnimationName)) {
            debugLog(
                "[CustomAnimationK2DrexlTest] ERROR: mapping %s:%s -> %s failed\n",
                DrexlModelName,
                triggerName,
                DrexlAttackAnimationName
            );
            return false;
        }
    }

    debugLog(
        "[CustomAnimationK2DrexlTest] Ready: id=%u model=%s animation=%s triggers=%u\n",
        DrexlAttackAnimationId,
        DrexlModelName,
        DrexlAttackAnimationName,
        static_cast<unsigned int>(sizeof(TriggerAnimationNames) / sizeof(TriggerAnimationNames[0]))
    );
    return true;
}
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        InstallDrexlProbe();
    }
    return TRUE;
}

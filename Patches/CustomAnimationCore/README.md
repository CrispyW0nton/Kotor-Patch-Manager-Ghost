# Custom Animation Core

Experimental Issue #98 patch scaffold for making additional model animations callable by engine consumers.

The first pass exposes a small exported registry that downstream patches can use to reserve animation names and map gameplay tuples to animation labels:

- `RegisterAnimation(const char* name) -> uint16_t`
- `RegisterAnimationWithId(const char* name, uint16_t id) -> bool`
- `MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName) -> bool`
- `LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) -> const char*`
- `LookupAnimationId(const char* name) -> uint16_t`
- `LookupAnimationNameById(uint16_t id) -> const char*`
- `ClearCustomAnimationRegistry() -> void`

Registry rules for the v0 prototype:

- `RegisterAnimationWithId` is the recommended path until the loader can append
  rows to `animations.2da`.
- Registering the same name with the same ID is idempotent.
- Registering the same name with a different ID fails.
- Registering a different name with an already-used ID fails.
- `MapWeaponAction` only accepts names that are already registered.

Resolver keys currently support `0xff` as a wildcard. Lookup order is exact
match, weapon wildcard, action wildcard, then global wildcard. This exists so
the smoke-test patch can prove the hook path before the parameter semantics are
fully named.

The first K1 prototype hooks the fallback/default return paths in:

- `CSWCCreature::UpdateMeleeAttackData`
- `CSWCCreature::UpdateRangedAttackData`

When vanilla falls through to those default paths, the hook resolves the raw resolver tuple through `MapWeaponAction` and swaps the returned animation ID if a registered mapping exists. This preserves vanilla hardcoded cases while creating an experimental escape hatch for unhandled tuples.

Until the loader hook can inject rows into `animations.2da`, downstream patches
should use `RegisterAnimationWithId` with an ID that is already valid in the
active `animations.2da` file. `RegisterAnimation` still exists for the future
loader-backed path, but auto-assigned IDs are not safe for release content yet.

`Patches/CustomAnimationSmokeTest` is the current development harness. It maps
the global wildcard to K1 row `17` (`victory`) so hook hits are visible in debug
output and, when the fallback path is reached, in-game behavior.

The reverse ID-to-name lookup is phase-3 scaffolding. It is intended for a
future `CSWCAnimBase::GetAnimationName` bypass so release patches can resolve
custom IDs without mutating the engine-owned `C2DA` table directly.

The prototype currently supports the K1 1.03 GOG and CD crack hashes already used by `ScriptExtender`.

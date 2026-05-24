# Custom Animation Core

Experimental Issue #98 patch scaffold for making additional model animations callable by engine consumers.

The first pass exposes a small exported registry that downstream patches can use to reserve animation names and map gameplay tuples to animation labels:

- `RegisterAnimation(const char* name) -> uint16_t`
- `RegisterAnimationWithId(const char* name, uint16_t id) -> bool`
- `MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName) -> bool`
- `MapResolverAnimation(uint8_t resolverFamily, uint8_t key1, uint8_t key2, const char* animName) -> bool`
- `MapAnimationIdOverride(uint16_t fromId, uint16_t toId) -> bool`
- `LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) -> const char*`
- `LookupRegisteredResolverAnim(uint8_t resolverFamily, uint8_t key1, uint8_t key2) -> const char*`
- `LookupAnimationId(const char* name) -> uint16_t`
- `LookupAnimationNameById(uint16_t id) -> const char*`
- `ClearCustomAnimationRegistry() -> void`

Registry rules for the v0 prototype:

- `RegisterAnimationWithId` is the recommended path until the loader can append
  rows to `animations.2da`.
- Registering the same name with the same ID is idempotent.
- Registering the same name with a different ID fails.
- Registering a different name with an already-used ID fails.
- `MapResolverAnimation` only accepts names that are already registered.
- `MapAnimationIdOverride` only accepts target IDs that are already registered.
- Direct play-name overrides may target any animation name. The model/supermodel
  chain is allowed to prove whether that name exists at runtime.
- `MapWeaponAction` is retained as a compatibility wrapper for family `0`
  mappings.

Resolver families are `0 = any`, `1 = melee`, and `2 = ranged`. Resolver keys
currently support `0xff` as a wildcard. Lookup order is family exact match,
family key wildcards, then the same sequence against the `any` family. This
exists so the smoke-test patch can prove the hook path before the parameter
semantics are fully named.

The first K1 prototype hooks the fallback/default return paths in:

- `CSWCCreature::UpdateMeleeAttackData`
- `CSWCCreature::UpdateRangedAttackData`
- `CSWCAnimBase::GetAnimationName`
- `CSWCAnimBase::SetAnimation`

When vanilla falls through to the combat default paths, the hook resolves the
raw resolver tuple through the family-aware resolver map and swaps the returned
animation ID if a registered mapping exists. This preserves vanilla hardcoded
cases while creating an experimental escape hatch for unhandled tuples.
These combat epilogue hooks are non-returning: they restore KPM's wrapper-saved
state and emulate the stolen game epilogue directly because the overwritten
bytes include `RET` instructions.

When `CSWCAnimBase::GetAnimationName` cannot resolve an ID through
`animations.2da`, the hook checks the registry by ID. On a hit, it writes the
registered animation name into the function's output `CExoString` and resumes
the vanilla success path. On a miss, it resumes the vanilla failure path.

`CSWCAnimBase::SetAnimation` is hooked at function entry so development patches
can remap a vanilla animation ID to a registered custom ID before playback is
forwarded to the concrete animation implementation. Live testing showed vanilla
idle and locomotion IDs are not safe proof points because remapping them can
leave run/walk state stuck, so the current smoke test leaves those IDs alone.
The hook remains useful for narrowly scoped scripted or interaction-based test
triggers. In the current asset pipeline, a malformed MDL animation block can
still crash earlier during model/animation footprint setup.

Until the loader hook can inject rows into `animations.2da`, downstream patches
should use `RegisterAnimationWithId` with an ID that is already valid in the
active `animations.2da` file. `RegisterAnimation` still exists for the future
loader-backed path, but auto-assigned IDs are not safe for release content yet.

`Patches/CustomAnimationSmokeTest` is the current development harness. It
registers proof ID `10000`, installs the family-agnostic wildcard resolver
mapping, and resolves observed save-load animation IDs back to the registered
name through the `GetAnimationName` bypass. A live control run mapped those same
IDs to vanilla `dance` and reached both `Base PlayAnimation` and
`Gob::PlayAnimation`, proving the registry/name path. The intended custom target
is currently `kpmwin1`; if it visually A-poses while the same IDs can play
`dance`, the remaining issue is the exported MDL animation payload rather than
the KPM hook path.

The prototype currently supports the K1 1.03 GOG and CD crack hashes already used by `ScriptExtender`.

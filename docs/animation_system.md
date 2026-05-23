# Animation System Investigation

Issue #98 asks for four things: how the animation system works, what the current limits are, why those limits exist, and how to work around them. This document captures the first K1 pass from the local address database and turns it into a patch plan.

## Summary

The practical limit is not the MDL format and not `animations.2da` row count. Models can carry additional named animations, and `animations.2da` is already treated as a 16-bit row space. The limiting layer is the engine code that decides which animation name or animation row to request for combat, dialog, cutscenes, force powers, and other gameplay actions.

The recommended shape is a small `CustomAnimationCore` patch that exposes a registry to downstream patches, then detours the engine consumers that build or resolve animation names. K1 should be the first target because its address database already has the relevant symbols. K2 support should follow once the matching K2 functions are labelled.

## Confirmed K1 Address-Database Entries

These entries were checked against `AddressDatabases/kotor1_0_3.db`.

| Area | Symbol | Address | Convention | Stack params |
|---|---|---:|---|---:|
| 2DA load | `CTwoDimArrays::Load2DArrays_Animations` | `0x005c32e0` | `__thiscall` | `0` |
| 2DA load | `CTwoDimArrays::Load2DArrays_DialogAnimations` | `0x005c3bf0` | `__thiscall` | `0` |
| 2DA load | `CTwoDimArrays::Load2DArrays_CombatAnimations` | `0x005c3b50` | `__thiscall` | `0` |
| Client resolver | `CClientExoApp::GetClientMeleeAnimation` | `0x005edce0` | `__thiscall` | `24` |
| Client resolver | `CClientExoApp::GetClientRangedAnimation` | `0x005edcc0` | `__thiscall` | `16` |
| Client resolver | `CClientExoAppInternal::GetClientMeleeAnimation` | `0x005f32e0` | `__thiscall` | `24` |
| Client resolver | `CClientExoAppInternal::GetClientRangedAnimation` | `0x005f33f0` | `__thiscall` | `16` |
| Server resolver | `CSWSCreature::ResolveMeleeAnimations` | `0x005b7470` | `__thiscall` | `20` |
| Server resolver | `CSWSCreature::ResolveRangedAnimations` | `0x005b6c40` | `__thiscall` | `12` |
| Playback | `Gob::PlayAnimation` | `0x00485bd0` | `__thiscall` | `16` |
| Playback | `Gob::PlayOutOfOrderAnimation` | `0x00485130` | `__thiscall` | `20` |
| Playback | `Gob::LoadAddInAnimations` | `0x00440890` | `__thiscall` | `4` |
| Playback | `Gob::RemoveAddInAnimations` | `0x0044b2c0` | `__thiscall` | `0` |
| Playback tick | `Gob::Animate` | `0x00486670` | `__thiscall` | `4` |
| Anim bridge | `CSWCAnimBase::SetAnimation` | `0x0069d4f0` | `__thiscall` | `16` |
| Anim bridge | `CSWCAnimBase::SetOverlayAnimation` | `0x0069e420` | `__thiscall` | `12` |
| Anim bridge | `CSWCAnimBase::AnimationExists` | `0x0069d910` | `__thiscall` | `2` |
| Anim bridge | `CSWCAnimBase::GetAnimationLength` | `0x0069d250` | `__thiscall` | `2` |
| Anim bridge | `CSWCAnimBase::GetAnimationName` | `0x0069e620` | `__thiscall` | `6` |

Important offsets:

| Class | Member | Offset | Type |
|---|---|---:|---|
| `CTwoDimArrays` | `animations` | `0x3c` | `C2DA *` |
| `CTwoDimArrays` | `dialoganimations` | `0x40` | `C2DA *` |
| `CTwoDimArrays` | `combatanimations` | `0x78` | `C2DA *` |
| `Animation` | `max_tree` | `0x0` | `MaxTree` |
| `AnimRun` | `animation` | `0x0` | `Animation *` |
| `CSWCAnimBase` | `gob` | `0xb8` | `Gob *` |
| `CSWSCombatRoundAction` | `animation_id` | `0x4` | `ushort` |
| `CSWSCombatRoundAction` | `animation` | `0x78` | `int` |
| `CSWSCombatAttackData` | `reaxn_animation` | `0x12` | `ushort` |
| `CSWSDialogAnimation` | `animation` | `0x8` | `ushort` |
| `CSWSDialogCamera` | `camera_animation` | `0x18` | `ushort` |
| `CSWCObject` | `looping_animation` | `0x58` | `ushort` |
| `CSWSObject` | `animation` | `0xd4` | `int` |

## Current Limits

| Layer | Limit | Notes |
|---|---|---|
| MDL model animation list | No fixed cap identified in this pass | The engine exposes dynamic animation structures, and the bottleneck appears after load, at consumer lookup time. |
| `animations.2da` | `65536` rows | Recorded in `docs/2da_row_limits.md`; enough for practical use. |
| `dialoganimations.2da` | `65536` rows | Also recorded in `docs/2da_row_limits.md`; row values are often masked as 16-bit. |
| `combatanimations.2da` | Effectively `uint32` | Recorded in `docs/2da_row_limits.md`. |
| Engine animation fields | Often `ushort` | Several combat/dialog/camera fields store animation IDs as 16-bit values. Widening these is high risk and probably unnecessary. |
| Gameplay consumers | Hardcoded | Combat, dialog, force-power, and cutscene paths only request the names/IDs their compiled logic knows how to build. |

## Cause

The data side can represent more animation names than the gameplay side asks for. The engine commonly reaches an animation by building or selecting a known animation key, resolving that through the 2DA/model path, and then passing the result to the `CSWCAnimBase`/`Gob` playback layer. New animation rows and model clips can exist, but a new gameplay tuple is invisible until a consumer is patched to ask for it.

That means Issue #98 is best treated as a consumer-resolution problem, not a raw model-capacity problem.

## Proposed Patch Plan

### Tier 1: Documentation

Keep this file as the public investigation artifact for Issue #98. Expand it as each resolver body is inspected in Ghidra.

### Tier 2: Registry API

Scaffolded in `Patches/CustomAnimationCore`.

The registry exports:

- `RegisterAnimation(const char* name) -> uint16_t`
- `RegisterAnimationWithId(const char* name, uint16_t id) -> bool`
- `MapResolverAnimation(uint8_t resolverFamily, uint8_t key1, uint8_t key2, const char* animName) -> bool`
- `MapWeaponAction(uint8_t weaponType, uint8_t actionKind, const char* animName) -> bool`
- `LookupRegisteredResolverAnim(uint8_t resolverFamily, uint8_t key1, uint8_t key2) -> const char*`
- `LookupRegisteredAnim(uint8_t weaponType, uint8_t actionKind) -> const char*`
- `LookupAnimationId(const char* name) -> uint16_t`
- `LookupAnimationNameById(uint16_t id) -> const char*`
- `ClearCustomAnimationRegistry() -> void`

`MapWeaponAction` and `LookupRegisteredAnim` are compatibility wrappers around
the family-agnostic resolver map. New code should prefer
`MapResolverAnimation`, where family `0` means any, `1` means melee, and `2`
means ranged. Resolver keys also support `0xff` wildcards.

Dynamic IDs currently begin at `65000`, leaving room under the `0xffff` invalid/sentinel value while staying above vanilla-style content.

### Tier 3: Loader Hook

Detour `CTwoDimArrays::Load2DArrays_Animations` after its Ghidra body is confirmed. The goal is to merge registered names into the loaded animation table or otherwise make name-to-ID lookups see registry entries.

Open questions:

- Whether the existing `C2DA` wrapper exposes enough mutation surface, or a new wrapper is needed.
- Whether registry data should be populated before or after the 2DA loader depending on DLL load order.
- Whether deterministic IDs should be fixed in TOML for release patches rather than allocated by first registration order.

### Tier 4: Resolver Hooks

Start with the client-side resolver path:

- `CClientExoAppInternal::GetClientMeleeAnimation`
- `CClientExoAppInternal::GetClientRangedAnimation`

The detour should check `(weaponType, actionKind)` in the registry and fall back to vanilla logic on a miss. Server-side resolver hooks should be treated as a second pass because they have higher gameplay and save-compatibility risk.

Prototype note: the first implementation hooks the fallback/default epilogues in `CSWCCreature::UpdateMeleeAttackData` at `0x0061406c` and `CSWCCreature::UpdateRangedAttackData` at `0x0061428c`. This is narrower than replacing the client resolver. It catches unhandled/default tuples, swaps `ESI` to the mapped animation ID, and then lets the original return sequence move that value into `EAX`.

### Runtime String Assignment Note

Live testing of the `GetAnimationName` hook showed that the registry override can fire while the visual playback still falls back to an A-pose if the output `CExoString` is populated incorrectly. Ghidra identifies `CExoString::operator=` at `0x005e5140` as:

```cpp
char** __thiscall CExoString::operator=(CExoString* this, char* value)
```

That means hook code must assign the resolved animation name by passing a raw null-terminated `char*` to the engine operator. Passing another `CExoString` object pointer into this overload is invalid and can make `CSWCAnimBase::GetAnimationName` report an override in telemetry without producing a usable playback name downstream.

### Local Animation MDL Footprint Note

Live tests with a GhostRigger-injected local `PMBAM` animation named `kpmwin1` reached a consistent K1 crash during save load:

- Windows fault: `swkotor.exe + 0x37f3c`, exception `0xc0000005`.
- Ghidra target: `UpdateAnimFootprint` at `0x00437db0`, faulting instruction `0x00437f3c`.
- Faulting access: the engine reads `[node + 0x30]` as child count, `[node + 0x2c]` as the converted child-pointer array, then dereferences `[childArray + index * 4]`.

The raw exported file is PyKotor-readable, has a full 61-node local animation tree, and has in-bounds raw child arrays. The crash happens after `InputBinary::ResetAnimation` calls `ResetMdlNode` and then `UpdateAnimFootprint`, so the current failure is likely a stricter runtime-layout expectation, not a registry/name-resolution failure.

One strong layout difference from vanilla supermodel animations: vanilla animation nodes are stored in depth-first tree order, with controller arrays/data generally after child subtrees. The current injected `kpmwin1` block stores each node's controller data before that node's children. KOTOR's footprint pass appears sensitive to that binary layout, even though a tolerant reader can resolve the offsets.

### Live Custom ID Proof and Current Asset Blocker

After GhostRigger switched the injected animation block to depth-first
serialization, the same save loaded without the `UpdateAnimFootprint` crash. The
next failure mode was a visual A-pose. Debug telemetry showed the save was
requesting high animation IDs such as `10030`, `10038` through `10042`, `10154`,
`10155`, and `10246`, not only the smoke-test proof ID `10000`.

`CustomAnimationCore` now lets `CSWCAnimBase::GetAnimationName` follow
`MapAnimationIdOverride` entries. With the observed save-load IDs mapped to
proof ID `10000`, the hook logs entries like:

```text
GetAnimationName override id 10030 -> 10000 (kpmwin1)
```

A control build then mapped the same IDs to vanilla `dance`. In game, the player
loaded into the save dancing. Runtime telemetry reached:

```text
Base PlayAnimation ... name=dance
Gob::PlayAnimation ... name=dance
```

That proves the KPM custom-ID/name path is viable. The remaining `kpmwin1`
A-pose is therefore an asset/export problem, not a registry or hook problem.

PyKotor inspection of the live Override `PMBAM` reports:

- local animation `kpmwin1`, root model `PMBAM`, length `10.0666666`
  seconds;
- `61` animation nodes matching the `PMBAM` model hierarchy exactly;
- `61` orientation controllers and `3` position controllers;
- `302` rows per controller.

However, the controller values contain very little actual motion:

- `42 / 61` orientation tracks move less than `0.1` degrees from first frame;
- only `2 / 61` orientation tracks move more than `5` degrees;
- pelvis/root position movement is about `0.02` game units at most.

The current GhostRigger-side blocker is likely not binary readability,
name-table casing, raw child traversal, or tree parity. It is that the reverse
retarget/export path is writing mostly rest-pose controller values. The next
asset-side fix should validate source-vs-export motion amplitude per mapped
bone before installing the MDL.

### Direct Name Lookup Diagnostic

The next diagnostic pass bypasses custom animation IDs entirely and maps direct
`Gob::PlayAnimation` name requests (`default`, `pause1`) to the local test name
`kpmwin1`. A plain C detour at `Gob::PlayAnimation` entry can log the incoming
stack string but cannot safely rewrite the live argument because KPM passes
stack parameters into detour callbacks as wrapper copies.

The working probe hooks `Gob::PlayAnimation` after `EBX` receives the animation
name at `0x00485bf6`. The naked hook updates KPM's saved `EBX` register slot,
restores CPU state, replays the stolen `push ebx; push 0x0073ee04` bytes, and
jumps back to the original string-check call at `0x00485bfc`.

Runtime findings:

- the register hook can change the real lookup query from `default` to
  `kpmwin1`;
- `FindAnimation` returns a fallback `default` animation when the current model
  does not contain the requested name, as seen on the `mainmenu` model;
- the current smoke hook now checks the Gob local/add-in model chain before
  mapping, logging `REGISTER_UNAVAILABLE` and keeping vanilla if the requested
  custom animation is absent;
- a PMBAM save-load pass should now produce either
  `REGISTER_MAPPED ... local_model=PMBAM ... resolved=kpmwin1`, proving engine
  lookup and `AnimRun` construction reach the custom animation, or
  `REGISTER_UNAVAILABLE ... local_model=PMBAM`, proving the live model chain
  does not expose the local animation despite offline MDL readback.

### Tier 5: Demo Patch

Use a small downstream patch to prove the contract. The likely demo is a K2 Mira/wrist-launcher animation patch, but K1 may need a temporary demo first because the K2 address databases are still sparse.

## Issue Comment Draft

I am planning to tackle #98 as a K1-first investigation/prototype, then carry it to K2 once the matching K2 resolver symbols are in the address databases.

My current read is that the hard limit is not the MDL animation list and not `animations.2da`; `animations.2da` already has a 65,536-row space. The limit is in the engine consumers that decide which animation name or row to request. The K1 DB already has the main stack labelled: `CTwoDimArrays::Load2DArrays_Animations` at `0x005c32e0`, client melee/ranged resolvers at `0x005f32e0`/`0x005f33f0`, server melee/ranged resolvers at `0x005b7470`/`0x005b6c40`, and the `CSWCAnimBase`/`Gob` playback layer.

Proposed deliverable: a small `CustomAnimationCore` patch that exports a registry for `animation name -> uint16 id` and `(weaponType, actionKind) -> animation name`, then detours the client resolver path to consult that registry before falling back to vanilla behavior. The loader hook and 2DA mutation details need Ghidra confirmation before I commit hook bytes. I have started the repo-side docs and scaffold on `codex/issue-98-custom-animations`.

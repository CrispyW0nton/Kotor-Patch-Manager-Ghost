# Custom Animation Patch Roadmap

This roadmap is the working plan for completing Issue #98, "Support for Additional Model Animations." The goal is not just to add one animation, but to create a reusable patch foundation that lets future KPM patches make new or previously unreachable model animations callable by the engine.

## Research Findings

The core bottleneck is consumer-side animation selection, not model capacity. `animations.2da` already has a 65,536-row limit in `docs/2da_row_limits.md`, `combatanimations.2da` is effectively `uint32`, and `dialoganimations.2da` is also 65,536 rows but frequently masked as `0xffff`.

JCarter426's combat-animation analysis establishes the important naming convention: animation labels encode class, weapon set, action, and variation. Examples include `g7g1`, where `g` is generic, `7` is rifle, `g` is dodge, and `1` is the variation. The same source explains that combat animations are rows in `animations.2da`, while `combatanimations.2da` maps combat outcomes onto those animation IDs.

Community research also confirms the current gameplay limitation. The lightsaber-form discussion says the game changes animation mainly by equipped item/weapon type, not arbitrary gameplay state such as saber form, and calls out Mira's wrist-launcher animations as existing content that cannot be reached without programming-side support.

The model-authoring side is viable but tool-sensitive. The MDL format stores animation offsets/counts in the model header, and the modding wiki points to MDLEdit, MDLOps, KOTORBlender, and KOTORMax as the relevant tools. DarthParametric and bead-v's animation workflow notes say KOTORMax can add animations and assign keyframes, but making the game use them is the separate engine-call problem this patch addresses.

## Current Prototype

Branch: `codex/issue-98-custom-animations`

Implemented:

- `Patches/CustomAnimationCore`
- `Patches/CustomAnimationSmokeTest`
- Exported registry API for animation name/ID mappings
- Reverse ID-to-name lookup for custom animation IDs
- Family-aware resolver lookup with wildcard support for smoke-test coverage
- K1 hooks on `CSWCCreature::UpdateMeleeAttackData` fallback epilogue at `0x0061406c`
- K1 hooks on `CSWCCreature::UpdateRangedAttackData` fallback epilogue at `0x0061428c`
- K1 hook on `CSWCAnimBase::GetAnimationName` post-2DA lookup branch at `0x0069e690`
- Build/package verification via `Patches/create-patch.bat`

Prototype limitation:

- It only overrides fallback/default combat animation IDs.
- It bypasses failed `animations.2da` ID-to-name lookups, but does not mutate the `C2DA` table.
- Downstream tests can currently map to explicitly registered IDs below `0xffff`.

## Roadmap

### Milestone 1: Prove the K1 Hook Contract

Goal: demonstrate that `CustomAnimationCore` can override an animation ID in live K1 combat without breaking vanilla cases.

Status: in progress. `CustomAnimationSmokeTest` now loads as a DLL-only dependent patch, registers `victory` as custom animation ID `65000`, and maps the family-agnostic global wildcard key `(0, 0xff, 0xff)` to that animation.

Tasks:

- Add a tiny test patch, `Patches/CustomAnimationSmokeTest`. Done.
- In `DllMain`, resolve `custom-animation-core.dll` exports with `GetProcAddress`. Done.
- Call `RegisterAnimationWithId("known_existing_anim", existingRowId)`. Done with `victory -> 65000`.
- Call `MapWeaponAction(testWeaponKey, testActionKey, "known_existing_anim")`. Superseded by `MapResolverAnimation`; done with wildcard `(0, 0xff, 0xff)`.
- Use a mapping that is easy to trigger in-game and falls through the hooked default path.
- Add debug logging for all hook hits, misses, and successful substitutions. Done in `CustomAnimationCore`.

Acceptance:

- Patch builds and packages.
- Game launches with both patches enabled.
- Debug output shows a hook hit and ID substitution.
- Vanilla combat still works when no mapping is present.

### Milestone 2: Stabilize Registry Semantics

Goal: make the API safe for real downstream patches.

Status: in progress. The registry now treats `(name, id)` registrations as stable pairs: duplicate same-name/same-ID registration is allowed, same-name/different-ID registration fails, different-name/same-ID registration fails, and resolver mappings require the animation name to be registered first. Resolver mappings are now split by family (`any`, `melee`, `ranged`) so melee and ranged keys can no longer collide.

Tasks:

- Replace the ambiguous `(uint8 weaponType, uint8 actionKind)` key names with documented resolver-key names once K1 parameter semantics are confirmed. In progress: `MapResolverAnimation` uses neutral `family/key1/key2` naming.
- Add separate map families if melee and ranged keys differ. Done.
- Add deterministic explicit-ID registration as the recommended v0 path. Done.
- Reserve dynamic auto-ID allocation for after loader/table mutation exists. Done in docs and mapping behavior; `RegisterAnimation` remains exported for future loader-backed work.
- Add duplicate-registration behavior to the README. Done.

Acceptance:

- A downstream patch can register the same name twice and get a stable result.
- Different patches cannot silently remap an existing animation name to a different ID.
- Registry behavior is documented enough for patch authors.

### Milestone 3: Add `animations.2da` Integration

Goal: remove the requirement that a custom animation already exists as a vanilla/override row.

Status: lookup-level bypass scaffold implemented. Direct `C2DA` mutation is not the first implementation target because the current GameAPI wrapper only exposes read calls and the internal row-storage layout is not fully labelled. `CSWCAnimBase::GetAnimationName` now has a K1 hook that lets registered custom IDs resolve to animation names after vanilla `animations.2da` lookup fails.

Tasks:

- Inspect `CTwoDimArrays::Load2DArrays_Animations` and `C2DA` memory layout in Ghidra. Started.
- Decide between true `C2DA` mutation and lookup-level bypass. Initial choice implemented: lookup-level bypass first.
- If mutating `C2DA`, add GameAPI support for row append or row-label lookup.
- Load a small patch-owned config, likely `additional/custom_animations.toml`, with explicit IDs and names.
- Ensure IDs are deterministic across launches and save/load.

Acceptance:

- A patch can ship a new animation label and ID without editing the user's base `animations.2da`.
- `LookupAnimationId` resolves the new label after 2DA load.
- Save/load remains stable because IDs are explicit, not order-dependent.

### Milestone 4: Expand Consumer Coverage

Goal: move beyond fallback combat cases into the actual hardcoded consumer matrix.

Tasks:

- Hook direct hardcoded returns in `UpdateMeleeAttackData`.
- Hook direct hardcoded returns in `UpdateRangedAttackData`.
- Evaluate client resolver hooks at `CClientExoAppInternal::GetClientMeleeAnimation` and `GetClientRangedAnimation`.
- Defer server-side `CSWSCreature::ResolveMeleeAnimations` and `ResolveRangedAnimations` until client-only behavior is proven.
- Create a consumer inventory table in `docs/animation_system.md`.

Acceptance:

- New mappings can override specific vanilla weapon/action/variation cases.
- Missing mapping always falls back to vanilla behavior.
- Hooks are narrow and byte-verified.

### Milestone 5: Build the Authoring/Test Pipeline

Goal: make a real asset-driven demo possible.

Tasks:

- Pick a K1-friendly smoke-test animation first.
- Define the model/supermodel edit workflow using MDLEdit and KOTORMax/KOTORBlender.
- Document where files go for K1 vs K2: K1 override root, K2 override subfolder support.
- Add a repeatable test module or creature spawn setup.
- Keep a small matrix of animation name, 2DA row, model file, trigger condition, and expected visual.

Acceptance:

- We can prove a real model animation plays, not just an ID swap.
- The demo can be reproduced from a clean install.

### Milestone 6: K2 RE and Mira Demo

Goal: bring the feature to the game where the strongest demo exists.

Tasks:

- Use Odyssey's K2 GOG/Aspyr program at `/TSL/k2_win_gog_aspyr_swkotor2.exe`.
- Locate K2 equivalents for:
  - `UpdateMeleeAttackData`
  - `UpdateRangedAttackData`
  - `GetClientMeleeAnimation`
  - `GetClientRangedAnimation`
  - `Load2DArrays_Animations`
- Add the confirmed symbols to both K2 address databases.
- Port K1 hooks to K2 hook files.
- Build the Mira/wrist-launcher demo after K2 hook parity is real.

Acceptance:

- K2 GOG/Aspyr and Steam/Aspyr address DBs include the required functions.
- K2 patch builds and applies version-specific hook files.
- Mira/wrist-launcher animation plays without replacing an unrelated animation set.

### Milestone 7: Release Candidate

Goal: ship `CustomAnimationCore` as a stable dependency patch.

Tasks:

- Split docs into user-facing README and engineering notes.
- Add conflict notes for any patch touching the same hook addresses.
- Prepare an Issue #98 comment with findings, patch shape, and current limitations.
- Prepare a PR with K1-first scope if K2 is not ready.
- Include a demo patch or smoke-test patch as proof.

Acceptance:

- `CustomAnimationCore` is useful without the demo patch.
- Downstream patches can require it cleanly.
- Known limitations are explicit.

## Risks

- KPM detours cannot call the full original function from C++; hooks must be narrow or fully replace behavior.
- K2 address databases currently lack the animation symbols locally, so K2 support depends on RE/import work.
- Runtime auto-assigned IDs are unsafe for save stability; release patches should prefer explicit IDs.
- Server-side combat hooks may have gameplay side effects and should come after client-side proof.
- Model authoring remains a separate workflow; this patch only makes engine consumers ask for the animation.

## Source Links

- JCarter426, combat animation naming and 2DA relationship: https://deadlystream.com/topic/4505-analysis-of-the-combat-animations/
- DeadlyStream animation/rigging workflow notes: https://deadlystream.com/topic/7475-editing-animations-and-rigging/
- Lightsaber-form limitation and Mira wrist-launcher context: https://deadlystream.com/topic/6501-advicerequest-new-attack-animations-for-lightsaber-forms/
- KotOR MDL format and editor links: https://kotor-modding.fandom.com/wiki/MDL_Format
- KOTOR engine/modding limits discussion: https://www.reddit.com/r/kotor/comments/cqit00/

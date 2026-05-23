# Custom Animation Smoke Test

Development-only proof patch for `custom-animation-core`.

This patch has no hook entries of its own. KPatchCore loads it as a DLL-only
patch after `custom-animation-core` because of the manifest dependency. On
attach it:

- resolves `custom-animation-core.dll` exports,
- registers `kpmwin1` as proof animation ID `10000`,
- verifies both name-to-ID and ID-to-name registry lookups,
- maps the family-agnostic wildcard resolver key `(0, 0xff, 0xff)` to that ID,
- maps the currently observed save-load probe IDs back to proof ID `10000`.

With both patches installed, any hooked fallback/default combat resolver path
should log a `CustomAnimationCore` override and return ID `10000`. The
`GetAnimationName` bypass then resolves that custom ID back to the model
animation name `kpmwin1`.

The current live save-load proof uses probe IDs observed in DebugView telemetry:
`10000`, `10001`, `10030`, `10038` through `10042`, `10154`, `10155`, and
`10246`. A temporary control build mapped those same IDs to vanilla `dance`; the
game visibly danced and telemetry reached `Base PlayAnimation` and
`Gob::PlayAnimation` with `name=dance`. That proves the KPM custom-ID/name path.
If `kpmwin1` still A-poses under the same mapping set, the remaining failure is
the GhostRigger-exported local animation data, not this registry proof path.

For local testing, install a supermodel MDL/MDX pair containing that animation
in Override. The current GhostRigger test asset must export the animation block
with the full target Aurora hierarchy, not only keyed bones, or KOTOR can crash
while building the animation footprint before the `SetAnimation` hook fires.
This is only for proving the hook contract; release demos should use explicit
resolver keys and package their asset files through KPM's `additional/` flow once
that installer path exists.

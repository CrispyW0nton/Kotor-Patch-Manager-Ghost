# Custom Animation Smoke Test

Development-only proof patch for `custom-animation-core`.

This patch has no hook entries of its own. KPatchCore loads it as a DLL-only
patch after `custom-animation-core` because of the manifest dependency. On
attach it:

- resolves `custom-animation-core.dll` exports,
- registers `victory` as custom animation ID `65000`,
- verifies both name-to-ID and ID-to-name registry lookups,
- maps the family-agnostic wildcard resolver key `(0, 0xff, 0xff)` to that ID.
- maps vanilla pause/idle animation IDs `6, 7, 8, 9, 12, 13, 14, 15` plus the
  observed loaded-save idle request `10000` to that ID for live idle smoke
  testing.

With both patches installed, any hooked fallback/default combat resolver path
should log a `CustomAnimationCore` override and return ID `65000`. The
`GetAnimationName` bypass then resolves that custom ID back to the model
animation name `victory`. The idle remap path should do the same when the engine
requests a vanilla pause/idle ID. The smoke test intentionally avoids the global
animation override now, because the wildcard path can stomp locomotion requests
and make run/walk appear frozen.

For local testing, install a supermodel MDL/MDX pair containing that animation
in Override. The current GhostRigger test asset must export the animation block
with the full target Aurora hierarchy, not only keyed bones, or KOTOR can crash
while building the animation footprint before the `SetAnimation` hook fires.
This is only for proving the hook contract; release demos should use explicit
resolver keys and package their asset files through KPM's `additional/` flow once
that installer path exists.

# Custom Animation Smoke Test

Development-only proof patch for `custom-animation-core`.

This patch has no hook entries of its own. KPatchCore loads it as a DLL-only
patch after `custom-animation-core` because of the manifest dependency. On
attach it:

- resolves `custom-animation-core.dll` exports,
- registers `victory` as custom animation ID `65000`,
- verifies both name-to-ID and ID-to-name registry lookups,
- maps the wildcard resolver key `(0xff, 0xff)` to that ID.

With both patches installed, any hooked fallback/default combat resolver path
should log a `CustomAnimationCore` override and return ID `65000`. The
`GetAnimationName` bypass then resolves that custom ID back to the normal model
animation name `victory`. This is only for proving the hook contract; release
demos should use explicit resolver keys and real custom animation IDs.

# Custom Animation Smoke Test

Development-only proof patch for `custom-animation-core`.

This patch has no hook entries of its own. KPatchCore loads it as a DLL-only
patch after `custom-animation-core` because of the manifest dependency. On
attach it:

- resolves `custom-animation-core.dll` exports,
- registers `cac_smoke_victory` as animation row `17` (`victory` in K1
  `animations.2da`),
- verifies both name-to-ID and ID-to-name registry lookups,
- maps the wildcard resolver key `(0xff, 0xff)` to that row.

With both patches installed, any hooked fallback/default combat resolver path
should log a `CustomAnimationCore` override and return row `17`. This is only
for proving the hook contract; release demos should use explicit resolver keys
and real custom animation rows.

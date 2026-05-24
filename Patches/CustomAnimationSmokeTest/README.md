# Custom Animation Smoke Test

Development-only proof patch for `custom-animation-core`.

This patch has no hook entries of its own. KPatchCore loads it as a DLL-only
patch after `custom-animation-core` because of the manifest dependency. On
attach it:

- resolves `custom-animation-core.dll` exports,
- registers `kpmwin1` for direct-name proof testing,
- verifies both name-to-ID and ID-to-name registry lookups,
- maps direct `Gob::PlayAnimation` names `default` and `pause1` to `kpmwin1`
  only for the test body models `PMBAL` and `PMBAM`, then lets the core
  availability gate confirm the local animation exists before replacing the
  engine's requested name.

The family-agnostic wildcard resolver proof remains in source, but is disabled
by default. Enable it only after the live character body model is confirmed to
contain `kpmwin1`; otherwise the engine can legitimately ask a model without
that local animation to play ID `10000`, which produces the same A-pose failure
we are trying to diagnose.

Earlier live testing used probe IDs observed in DebugView telemetry: `10000`,
`10001`, `10030`, `10038` through `10042`, `10154`, `10155`, and `10246`. A
temporary control build mapped those same IDs to vanilla `dance`; the game
visibly danced and telemetry reached `Base PlayAnimation` and
`Gob::PlayAnimation` with `name=dance`. That proves the KPM custom-ID/name path
is viable when the requested animation name exists on the live model chain.

The current `AnimationTest` save has also shown that the player body can be
`PMBAL`, while the first GhostRigger test asset installed `kpmwin1` only into
`PMBAM`. A temporary PMBAM-to-PMBAL rename is not a valid workaround and can
crash during save load. The next test asset should inject the animation into
the actual vanilla body model used by the save, or the save/outfit should be
changed to one that really loads PMBAM.

The smoke test deliberately avoids global play-name remaps. Placeables, doors,
heads, and other non-body models can play `default`/`pause1` too, so the proof
mapping stays scoped to the model names under test.

The first model-scoped live load pass reached gameplay without crashing. The
player body was `PMBAL`; `walk` stayed vanilla, and `pause1` logged
`REGISTER_UNAVAILABLE` because `PMBAL` still did not expose local animation
`kpmwin1`. That confirmed the runtime patch was behaving safely while the asset
route was still wrong.

## Custom Supermodel Probe

The current probe uses a separate test supermodel instead of injecting the
custom animation directly into PMBAM/PMBAL:

```text
PMBAM -> S_KPMF02 -> S_Female01
PMBAL -> S_KPMF02 -> S_Female01
```

`S_KPMF02` is a byte-preserved duplicate of vanilla `S_Female02` with one
additional local animation, `kpmwin1`, appended by GhostRigger's R3.B writer.
The PMBAM/PMBAL overrides are header-only edits that change the 32-byte
supermodel field from `S_Female02` to `S_KPMF02`; their MDX files remain vanilla
copies.

Build/rebuild the probe assets with:

```powershell
python tools\build_custom_supermodel_probe.py
```

Generated probe files live in `additional/`:

- `s_kpmf02.mdl/.mdx`
- `pmbam.mdl/.mdx`
- `pmbal.mdl/.mdx`
- `custom_supermodel_probe_manifest.json`

Live-test expectation: with `CustomAnimationCore` and this smoke test enabled,
the first PMBAM/PMBAL `default` or `pause1` request should map to `kpmwin1`,
`FindAnimation ADDIN RESULT` should report a non-null result from `S_KPMF02`,
and `AnimRun CREATE` should show `name=kpmwin1`. If that still A-poses, the
remaining bug is inside the exported animation block/controller semantics, not
the registry or the local-body routing.

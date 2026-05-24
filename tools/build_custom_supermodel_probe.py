"""Build the Issue #98 custom-supermodel probe assets.

This script uses GhostRigger's current reverse-retarget writer to produce a
separate supermodel resource that carries the custom ``kpmwin1`` animation,
then creates header-only PMBAM/PMBAL body overrides that inherit from it.

Output is written into ``Patches/CustomAnimationSmokeTest/additional`` so the
exact test assets are kept with the smoke patch source. Large R3.A intermediate
JSON files are written under ``tmp_custom_anim_probe/`` instead of the patch
folder.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GHOSTRIGGER = Path(
    r"C:\Users\NewAdmin\Documents\GDeveloper\Workspaces\Kotor-3D-Model-Converter-qt"
)
DEFAULT_FBX = Path(
    r"C:\Users\NewAdmin\Documents\KaiGenInteractive\AnimationLibrary\Exports\M_Neutral_Stand_Idle_Loop_export.fbx"
)
DEFAULT_OUT = ROOT / "Patches" / "CustomAnimationSmokeTest" / "additional"
DEFAULT_WORK = ROOT / "tmp_custom_anim_probe" / "custom_supermodel_probe_work"
DEFAULT_BODY_SOURCE = DEFAULT_GHOSTRIGGER / "tests" / "fixtures" / "kotor_stock" / "k1"
DEFAULT_PMBAL_SOURCE = DEFAULT_GHOSTRIGGER / "exports" / "pmbal_vanilla_extract"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def patch_supermodel_pointer(source_mdl: Path, source_mdx: Path, output_dir: Path, new_supermodel: str) -> dict:
    data = bytearray(source_mdl.read_bytes())
    field_abs = 12 + 80 + 56
    old = bytes(data[field_abs:field_abs + 32]).split(b"\0", 1)[0].decode("ascii", errors="replace")
    encoded = new_supermodel.encode("ascii")[:32].ljust(32, b"\0")
    data[field_abs:field_abs + 32] = encoded

    output_mdl = output_dir / source_mdl.name.lower()
    output_mdx = output_dir / source_mdx.name.lower()
    output_mdl.write_bytes(data)
    shutil.copyfile(source_mdx, output_mdx)
    return {
        "source_mdl": str(source_mdl),
        "output_mdl": str(output_mdl),
        "output_mdx": str(output_mdx),
        "old_supermodel": old,
        "new_supermodel": new_supermodel,
        "mdl_sha256": sha256(output_mdl),
        "mdx_sha256": sha256(output_mdx),
    }


def build_assets(args: argparse.Namespace) -> int:
    ghostrigger_root = Path(args.ghostrigger_root)
    if not ghostrigger_root.exists():
        raise FileNotFoundError(f"GhostRigger workspace not found: {ghostrigger_root}")
    sys.path.insert(0, str(ghostrigger_root))

    from src.core.retargeting.animation_injector import AnimationInjectionRequest, AnimationInjector
    from src.core.retargeting.aurora_animation_writer import AuroraAnimationInjectionRequest, AuroraAnimationWriter
    from src.core.retargeting.retarget_output_naming import KotorOutputAnimationNameMode

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    work_dir = Path(args.work_dir)
    work_dir.mkdir(parents=True, exist_ok=True)

    target_mdl = Path(args.supermodel_mdl)
    target_mdx = Path(args.supermodel_mdx)
    source_fbx = Path(args.source_fbx)
    output_supermodel_mdl = output_dir / f"{args.supermodel_resref.lower()}.mdl"

    extraction = AnimationInjector().inject(
        AnimationInjectionRequest(
            source_fbx=source_fbx,
            target_mdl=target_mdl,
            target_mdx=target_mdx,
            target_slot=args.animation_name,
            output_dir=work_dir,
            frame_step=args.frame_step,
            game="K1",
        )
    )
    if not extraction.success or extraction.retargeted_animation_json is None:
        raise RuntimeError("R3.A extraction failed: " + "; ".join(extraction.errors))

    writer_request = AuroraAnimationInjectionRequest(
        r3a_animation_json=extraction.retargeted_animation_json,
        target_mdl=target_mdl,
        target_mdx=target_mdx,
        animation_slot=args.animation_name,
        output_mdl=output_supermodel_mdl,
        output_manifest=work_dir / f"{args.supermodel_resref.lower()}__{args.animation_name}__manifest.json",
        game="K1",
        fps=extraction.fps,
        kotor_output_name_mode=KotorOutputAnimationNameMode.CUSTOM_PATCH,
        requires_custom_animation_patch=True,
        verify_roundtrip=False,
    )
    writer_result = AuroraAnimationWriter().inject(writer_request)
    if not writer_result.success:
        raise RuntimeError("R3.B supermodel injection failed: " + "; ".join(writer_result.errors))

    body_source = Path(args.body_source)
    pmbal_source = Path(args.pmbal_source)
    body_outputs = [
        patch_supermodel_pointer(
            body_source / "pmbam.mdl",
            body_source / "pmbam.mdx",
            output_dir,
            args.supermodel_resref,
        ),
        patch_supermodel_pointer(
            pmbal_source / "pmbal.mdl",
            pmbal_source / "pmbal.mdx",
            output_dir,
            args.supermodel_resref,
        ),
    ]

    manifest = {
        "probe": "custom_supermodel_inheritance",
        "animation_name": args.animation_name,
        "supermodel_resref": args.supermodel_resref,
        "chain": {
            "PMBAM": args.supermodel_resref,
            "PMBAL": args.supermodel_resref,
            args.supermodel_resref: "S_Female02 byte-preserved duplicate with appended local animation",
        },
        "source_fbx": str(source_fbx),
        "source_supermodel_mdl": str(target_mdl),
        "output_supermodel_mdl": str(output_supermodel_mdl),
        "output_supermodel_mdx": str(output_supermodel_mdl.with_suffix(".mdx")),
        "output_supermodel_mdl_sha256": sha256(output_supermodel_mdl),
        "output_supermodel_mdx_sha256": sha256(output_supermodel_mdl.with_suffix(".mdx")),
        "body_overrides": body_outputs,
        "r3a": extraction.to_dict(),
        "r3b": writer_result.to_dict(),
    }
    manifest_path = output_dir / "custom_supermodel_probe_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(json.dumps({
        "success": True,
        "manifest": str(manifest_path),
        "supermodel_mdl": str(output_supermodel_mdl),
        "supermodel_mdx": str(output_supermodel_mdl.with_suffix(".mdx")),
        "body_overrides": [entry["output_mdl"] for entry in body_outputs],
    }, indent=2))
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build KPM custom-supermodel probe assets")
    parser.add_argument("--ghostrigger-root", type=Path, default=DEFAULT_GHOSTRIGGER)
    parser.add_argument("--source-fbx", type=Path, default=DEFAULT_FBX)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--work-dir", type=Path, default=DEFAULT_WORK)
    parser.add_argument("--body-source", type=Path, default=DEFAULT_BODY_SOURCE)
    parser.add_argument("--pmbal-source", type=Path, default=DEFAULT_PMBAL_SOURCE)
    parser.add_argument("--supermodel-mdl", type=Path, default=DEFAULT_BODY_SOURCE / "S_Female02.mdl")
    parser.add_argument("--supermodel-mdx", type=Path, default=DEFAULT_BODY_SOURCE / "S_Female02.mdx")
    parser.add_argument("--supermodel-resref", default="S_KPMF02")
    parser.add_argument("--animation-name", default="kpmwin1")
    parser.add_argument("--frame-step", type=int, default=1)
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(build_assets(parse_args()))

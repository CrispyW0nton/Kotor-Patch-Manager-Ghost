"""Build the KOTOR 2 Mykal-to-Drexl custom-animation proof asset.

The script uses Ghost Studio's KOTOR-to-KOTOR retarget and animation-only MDL
injection pipeline. It reads both source models from the stock K2 installation,
retargets Mykal's ``g0a1`` attack onto the flying Drexl hierarchy, and writes an
Override-ready ``c_drexlf.mdl/.mdx`` pair containing ``kpm_drx_a1``.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_GHOST_STUDIO = REPO_ROOT.parent / "Ghost-Studio"
DEFAULT_K2_DIR = Path(
    r"C:\Program Files (x86)\Steam\steamapps\common\Knights of the Old Republic II"
)
DEFAULT_OUTPUT_DIR = REPO_ROOT / "Patches" / "CustomAnimationK2DrexlTest" / "additional"


def _configure_ghost_studio_imports(root: Path) -> None:
    roots: list[Path] = []
    for project_root in sorted((root / "native").glob("GhostRigger.*")):
        python_root = project_root / "Python"
        src_root = python_root / "src"
        if src_root.exists():
            roots.append(src_root)
        if python_root.exists():
            roots.append(python_root)
    roots.extend((root / "src", root))

    for path in reversed(roots):
        if path.exists() and str(path) not in sys.path:
            sys.path.insert(0, str(path))


def _make_export_manifest_portable(manifest_path: Path | None) -> None:
    if manifest_path is None or not manifest_path.is_file():
        return

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for field in ("manifest_path", "output_mdl", "output_mdx"):
        value = manifest.get(field)
        if value:
            manifest[field] = Path(value).name
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def _drexl_profile():
    from src.core.retargeting.retarget_profile import RetargetMappingEntry, RetargetProfile

    # One-to-one controls are intentional. Mykal has no forelimb skeleton, so
    # Drexl's arms and fingers remain on keyed target-rest controllers here.
    mapping_rows = (
        ("root", "C_Mykal", "C_DrexlF", "center", False),
        ("helper", "cutscenedummy", "cutscenedummy", "center", True),
        ("root", "Rootdummy", "rootdummy", "center", True),
        ("pelvis", "TorsoLwr_g", "pelvis_g", "center", False),
        ("spine", "TorsoMid_g", "torso1_g", "center", False),
        ("chest", "TorsoUpr_g", "torso3_g", "center", False),
        ("tail_base", "TailBase_g", "tail1_g", "center", False),
        ("tail_lower", "TailLwr_g", "tail2_g", "center", False),
        ("tail_mid", "TailMid_g", "tail4_g", "center", False),
        ("tail_tip", "TailTip_g", "tail6_g", "center", False),
        ("neck", "Neck_g", "neck_g", "center", False),
        ("head", "Head_g", "head_g", "center", False),
        ("wing_base", "LWingBase_g", "Lwing_01", "left", False),
        ("wing_lower", "LWingLwr_g", "Lwing_02", "left", False),
        ("wing_mid", "LWingMid_g", "Lwing_03", "left", False),
        ("wing_tip", "LWingTip_g", "Lwing_04", "left", False),
        ("wing_flap", "LWingFlap_g", "Lwing_05", "left", False),
        ("wing_base", "RWingBase_g", "Rwing_01", "right", False),
        ("wing_lower", "RWingLwr_g", "Rwing_02", "right", False),
        ("wing_mid", "RWingMid_g", "Rwing_03", "right", False),
        ("wing_tip", "RWingTip_g", "Rwing_04", "right", False),
        ("wing_flap", "RWingFlap_g", "Rwing_05", "right", False),
    )
    return RetargetProfile(
        version=1,
        name="k2_c_mykal_to_c_drexlf",
        source_clip_hint="K2:c_mykal:g0a1",
        target_model_hint="K2:c_drexlf",
        animation_slot="kpm_drx_a1",
        source_reference={"mode": "clip_rest"},
        target_reference={"mode": "target_rest"},
        mappings=[
            RetargetMappingEntry(
                role=role,
                source_node=source,
                target_node=target,
                side=side,
                allow_helper_mapping=allow_helper,
            )
            for role, source, target, side, allow_helper in mapping_rows
        ],
        metadata={
            "generated_by": "build_k2_drexl_animation_probe",
            "source_game": "K2",
            "target_game": "K2",
            "source_animation": "g0a1",
            "output_animation": "kpm_drx_a1",
            "key_unmapped_reference_nodes": True,
            "known_limit": "Mykal has no forelimb controls; Drexl forelimbs remain at target rest.",
        },
    )


def build(args: argparse.Namespace) -> dict[str, object]:
    ghost_studio = Path(args.ghost_studio).resolve()
    _configure_ghost_studio_imports(ghost_studio)

    from src.core.assets.resource_manager import ResourceManager
    from src.core.assets.resource_manager import RES_UTC
    from src.core.retargeting.kotor_to_kotor_preview import (
        KotorToKotorPreviewRequest,
        build_kotor_to_kotor_retarget_preview,
    )
    from src.core.retargeting.retarget_output_naming import (
        KotorOutputAnimationNameMode,
        RetargetOutputNaming,
    )
    from src.core.retargeting.retarget_preview_export import (
        RetargetPreviewExportRequest,
        export_retarget_preview_override,
    )
    from src.core.retargeting.retarget_solver import RetargetSolverOptions
    from pykotor.resource.formats.gff import bytes_gff, read_gff

    manager = ResourceManager()
    if not manager.set_k2_dir(str(Path(args.k2_dir).resolve())):
        raise RuntimeError(f"Could not index the KOTOR 2 installation: {args.k2_dir}")

    source = manager.load_model(args.source_model, "K2")
    target = manager.load_model(args.target_model, "K2")
    if source is None or target is None:
        raise RuntimeError(
            f"Could not load K2 source/target models: {args.source_model}, {args.target_model}"
        )

    profile = _drexl_profile()
    profile.animation_slot = args.output_animation
    naming = RetargetOutputNaming(
        kotor_name_mode=KotorOutputAnimationNameMode.CUSTOM_PATCH,
        requested_kotor_animation_name=args.output_animation,
        display_label="Mykal attack on flying Drexl",
    )
    preview_result = build_kotor_to_kotor_retarget_preview(
        KotorToKotorPreviewRequest(
            source_model=source,
            source_animation_slot=args.source_animation,
            target_model=target,
            retarget_profile=profile,
            output_naming=naming,
            source_sample_rate=float(args.sample_rate),
            solver_options=RetargetSolverOptions(
                rotation_transfer_mode="reference_frame_delta",
                key_unmapped_reference_nodes=True,
                strict=True,
            ),
            auto_play=False,
            enable_numeric_audit=True,
        )
    )

    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    output_mdl = output_dir / "c_drexlf.mdl"
    output_mdx = output_dir / "c_drexlf.mdx"
    export_result = export_retarget_preview_override(
        RetargetPreviewExportRequest(
            preview_result=preview_result.preview_result,
            original_target_model=target,
            output_mdl_path=output_mdl,
            output_mdx_path=output_mdx,
            overwrite=True,
            replace_existing=True,
            verify_roundtrip=True,
            write_manifest=True,
            kotor_output_name_mode=KotorOutputAnimationNameMode.CUSTOM_PATCH,
            requires_custom_animation_patch=True,
            target_mdl_bytes=manager.get_mdl(args.target_model, "K2"),
            target_mdx_bytes=manager.get_mdx(args.target_model, "K2"),
        )
    )
    _make_export_manifest_portable(export_result.manifest_path)

    test_utc_source = manager.get("c_drexl_amb", RES_UTC, "K2")
    if not test_utc_source:
        raise RuntimeError("Could not load the stock/modded K2 c_drexl_amb.utc template")
    test_utc = read_gff(test_utc_source)
    test_utc.root.set_resref("TemplateResRef", "kpm98_drx")
    test_utc.root.set_string("Tag", "kpm98_drexl")
    test_utc_path = output_dir / "kpm98_drx.utc"
    test_utc_path.write_bytes(bytes_gff(test_utc))

    preview = preview_result.preview_result
    summary = {
        "source_model": source.name,
        "source_animation": args.source_animation,
        "target_model": target.name,
        "output_animation": preview.slot_name,
        "duration_seconds": float(preview.animation_block.length),
        "mapped_node_count": int(preview.solver_report.mapped_node_count),
        "exported_node_count": len(preview.animation_block.nodes),
        "preview_audit_passed": bool(preview.preview_audit.passed),
        "roundtrip_verified": bool(export_result.verified_roundtrip),
        "mdl_path": str(export_result.mdl_path),
        "mdx_path": str(export_result.mdx_path),
        "manifest_path": str(export_result.manifest_path) if export_result.manifest_path else None,
        "test_utc_path": str(test_utc_path),
        "test_utc_resref": "kpm98_drx",
        "warnings": list(export_result.warnings),
    }
    print(json.dumps(summary, indent=2))
    return summary


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ghost-studio", type=Path, default=DEFAULT_GHOST_STUDIO)
    parser.add_argument("--k2-dir", type=Path, default=DEFAULT_K2_DIR)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--source-model", default="c_mykal")
    parser.add_argument("--target-model", default="c_drexlf")
    parser.add_argument("--source-animation", default="g0a1")
    parser.add_argument("--output-animation", default="kpm_drx_a1")
    parser.add_argument("--sample-rate", type=float, default=20.0)
    return parser


if __name__ == "__main__":
    build(_parser().parse_args())

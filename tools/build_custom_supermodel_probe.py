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
import struct
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
DEFAULT_SUPERMODEL_RESREF = "S_KPMF0200"
MDL_BASE = 12
MODEL_FIELDS_ABS = MDL_BASE + 80
MODEL_ANIM_ARRAY_OFF_ABS = MODEL_FIELDS_ABS + 8
MODEL_ANIM_COUNT_ABS = MODEL_FIELDS_ABS + 12
MODEL_ANIM_COUNT2_ABS = MODEL_FIELDS_ABS + 16


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


def _align4(value: int) -> int:
    return (value + 3) & ~3


def _u32(data: bytes | bytearray, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def _read_animation_offsets(mdl_bytes: bytes) -> list[int]:
    count = _u32(mdl_bytes, MODEL_ANIM_COUNT_ABS)
    table_rel = _u32(mdl_bytes, MODEL_ANIM_ARRAY_OFF_ABS)
    table_abs = MDL_BASE + table_rel
    offsets: list[int] = []
    if count <= 0 or table_rel <= 0 or table_abs + count * 4 > len(mdl_bytes):
        return offsets
    for index in range(count):
        anim_rel = _u32(mdl_bytes, table_abs + index * 4)
        if 0 < MDL_BASE + anim_rel < len(mdl_bytes):
            offsets.append(anim_rel)
    return offsets


def _read_animation_name(mdl_bytes: bytes, anim_rel: int) -> str:
    start = MDL_BASE + anim_rel + 8
    end = start + 32
    return mdl_bytes[start:end].split(b"\0", 1)[0].decode("ascii", errors="replace")


def _patch_relocated_offset(
    block: bytearray,
    field_abs_in_block: int,
    *,
    old_anim_rel: int,
    old_end_rel: int,
    delta: int,
) -> None:
    value = _u32(block, field_abs_in_block)
    if old_anim_rel <= value < old_end_rel:
        _write_u32(block, field_abs_in_block, value + delta)


def _collect_raw_animation_nodes(mdl_bytes: bytes, anim_rel: int) -> set[int]:
    """Return the raw animation node offsets reachable from one stock block."""

    anim_abs = MDL_BASE + anim_rel
    root_rel = _u32(mdl_bytes, anim_abs + 0x28)
    visited: set[int] = set()

    def walk(node_rel: int) -> None:
        if node_rel == 0 or node_rel in visited:
            return
        node_abs = MDL_BASE + node_rel
        if node_abs < 0 or node_abs + 80 > len(mdl_bytes):
            return
        visited.add(node_rel)
        child_array_rel = _u32(mdl_bytes, node_abs + 0x2C)
        child_count = _u32(mdl_bytes, node_abs + 0x30)
        child_array_abs = MDL_BASE + child_array_rel
        if child_count <= 0 or child_array_abs + child_count * 4 > len(mdl_bytes):
            return
        for index in range(child_count):
            walk(_u32(mdl_bytes, child_array_abs + index * 4))

    walk(root_rel)
    return visited


def append_relocated_stock_animation(
    source_mdl: Path,
    source_mdx: Path,
    output_mdl: Path,
    source_animation_name: str,
    output_animation_name: str,
) -> dict:
    """Append a raw relocated copy of a stock animation block.

    This is intentionally different from GhostRigger's retarget writer path.
    For vanilla-control probes we want the animation block's original sparse
    supermodel topology to survive byte-for-byte except for relocation fields
    and the fixed-width animation name. KOTOR's UpdateAnimFootprint path is
    sensitive to that raw tree shape.
    """

    mdl_bytes = bytearray(source_mdl.read_bytes())
    mdx_bytes = source_mdx.read_bytes()
    old_offsets = _read_animation_offsets(mdl_bytes)
    source_anim_rel = next(
        (
            offset
            for offset in old_offsets
            if _read_animation_name(mdl_bytes, offset).lower() == source_animation_name.lower()
        ),
        None,
    )
    if source_anim_rel is None:
        available = ", ".join(_read_animation_name(mdl_bytes, offset) for offset in old_offsets)
        raise RuntimeError(
            f"Source animation {source_animation_name!r} not found in {source_mdl}. "
            f"Available local animations: {available}"
        )

    source_end_rel = min((offset for offset in old_offsets if offset > source_anim_rel), default=len(mdl_bytes) - MDL_BASE)
    source_anim_abs = MDL_BASE + source_anim_rel
    source_end_abs = MDL_BASE + source_end_rel
    if source_anim_abs < 0 or source_end_abs > len(mdl_bytes) or source_end_abs <= source_anim_abs:
        raise RuntimeError(
            f"Could not determine a valid raw block for animation {source_animation_name!r}: "
            f"0x{source_anim_rel:x}..0x{source_end_rel:x}"
        )

    raw_block = bytearray(mdl_bytes[source_anim_abs:source_end_abs])
    encoded_name = output_animation_name.encode("ascii")[:32].ljust(32, b"\0")
    raw_block[8:40] = encoded_name

    out = bytearray(mdl_bytes)
    table_abs = _align4(len(out))
    out.extend(b"\0" * (table_abs - len(out)))
    anim_table_rel = table_abs - MDL_BASE
    kept_offsets = list(old_offsets)
    new_count = len(kept_offsets) + 1
    out.extend(b"\0" * (new_count * 4))

    new_anim_abs = _align4(len(out))
    out.extend(b"\0" * (new_anim_abs - len(out)))
    new_anim_rel = new_anim_abs - MDL_BASE
    delta = new_anim_rel - source_anim_rel

    # Animation header fields.
    _patch_relocated_offset(raw_block, 0x28, old_anim_rel=source_anim_rel, old_end_rel=source_end_rel, delta=delta)
    _patch_relocated_offset(raw_block, 0x78, old_anim_rel=source_anim_rel, old_end_rel=source_end_rel, delta=delta)

    for node_rel in sorted(_collect_raw_animation_nodes(mdl_bytes, source_anim_rel)):
        node_abs = MDL_BASE + node_rel
        block_node_abs = node_abs - source_anim_abs
        for field_rel in (0x08, 0x0C, 0x2C, 0x38, 0x44):
            _patch_relocated_offset(
                raw_block,
                block_node_abs + field_rel,
                old_anim_rel=source_anim_rel,
                old_end_rel=source_end_rel,
                delta=delta,
            )

        child_array_rel = _u32(mdl_bytes, node_abs + 0x2C)
        child_count = _u32(mdl_bytes, node_abs + 0x30)
        if child_count > 0 and source_anim_rel <= child_array_rel < source_end_rel:
            block_child_array_abs = (MDL_BASE + child_array_rel) - source_anim_abs
            for index in range(child_count):
                _patch_relocated_offset(
                    raw_block,
                    block_child_array_abs + index * 4,
                    old_anim_rel=source_anim_rel,
                    old_end_rel=source_end_rel,
                    delta=delta,
                )

    out.extend(raw_block)
    for index, offset in enumerate(kept_offsets + [new_anim_rel]):
        _write_u32(out, table_abs + index * 4, offset)

    _write_u32(out, 4, len(out) - MDL_BASE)
    _write_u32(out, 8, len(mdx_bytes))
    _write_u32(out, MODEL_ANIM_ARRAY_OFF_ABS, anim_table_rel)
    _write_u32(out, MODEL_ANIM_COUNT_ABS, new_count)
    _write_u32(out, MODEL_ANIM_COUNT2_ABS, new_count)

    output_mdl.write_bytes(out)
    output_mdl.with_suffix(".mdx").write_bytes(mdx_bytes)
    return {
        "mode": "raw_relocated_stock_animation",
        "source_mdl": str(source_mdl),
        "source_animation": source_animation_name,
        "output_animation": output_animation_name,
        "source_animation_rel": source_anim_rel,
        "source_animation_end_rel": source_end_rel,
        "output_animation_rel": new_anim_rel,
        "relocation_delta": delta,
        "reachable_source_nodes": len(_collect_raw_animation_nodes(mdl_bytes, source_anim_rel)),
        "mdl_sha256": sha256(output_mdl),
        "mdx_sha256": sha256(output_mdl.with_suffix(".mdx")),
    }


def patch_internal_model_resref(mdl_path: Path, old_resref: str, new_resref: str) -> dict:
    """Patch fixed-width and C-string name slots when cloning a supermodel."""
    if len(new_resref.encode("ascii")) > 32:
        raise ValueError(f"MDL resref is too long for fixed name slot: {new_resref}")
    if len(new_resref.encode("ascii")) != len(old_resref.encode("ascii")):
        raise ValueError(
            "Internal supermodel renames must preserve C-string length so MDL name offsets remain valid: "
            f"{old_resref!r} -> {new_resref!r}"
        )

    old_slot = old_resref.encode("ascii")[:32].ljust(32, b"\0")
    new_slot = new_resref.encode("ascii")[:32].ljust(32, b"\0")
    old_cstring = old_resref.encode("ascii") + b"\0"
    new_cstring = new_resref.encode("ascii") + b"\0"

    data = mdl_path.read_bytes()
    cstring_count = data.count(old_cstring)
    data = data.replace(old_cstring, new_cstring)
    slot_count = data.count(old_slot)
    data = data.replace(old_slot, new_slot)
    if cstring_count == 0 and slot_count == 0:
        raise RuntimeError(f"Did not find internal model resref {old_resref!r} in {mdl_path}")

    mdl_path.write_bytes(data)
    return {
        "path": str(mdl_path),
        "old_resref": old_resref,
        "new_resref": new_resref,
        "cstring_replacements": cstring_count,
        "slot_replacements": slot_count,
        "mdl_sha256": sha256(mdl_path),
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

    if args.clone_only_supermodel:
        shutil.copyfile(target_mdl, output_supermodel_mdl)
        shutil.copyfile(target_mdx, output_supermodel_mdl.with_suffix(".mdx"))
        extraction = None
        writer_result = None
        raw_duplicate = None
    elif args.duplicate_source_animation:
        raw_duplicate = append_relocated_stock_animation(
            target_mdl,
            target_mdx,
            output_supermodel_mdl,
            args.duplicate_source_animation,
            args.animation_name,
        )
        extraction = None
        writer_result = None
    else:
        raw_duplicate = None
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

    supermodel_identity_patch = patch_internal_model_resref(
        output_supermodel_mdl,
        target_mdl.stem,
        args.supermodel_resref,
    )

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
        "clone_only_supermodel": bool(args.clone_only_supermodel),
        "duplicate_source_animation": args.duplicate_source_animation,
        "chain": {
            "PMBAM": args.supermodel_resref,
            "PMBAL": args.supermodel_resref,
            args.supermodel_resref: (
                "S_Female02 byte-preserved duplicate"
                if args.clone_only_supermodel
                else "S_Female02 byte-preserved duplicate with appended local animation"
            ),
        },
        "source_fbx": str(source_fbx),
        "source_supermodel_mdl": str(target_mdl),
        "output_supermodel_mdl": str(output_supermodel_mdl),
        "output_supermodel_mdx": str(output_supermodel_mdl.with_suffix(".mdx")),
        "output_supermodel_mdl_sha256": sha256(output_supermodel_mdl),
        "output_supermodel_mdx_sha256": sha256(output_supermodel_mdl.with_suffix(".mdx")),
        "supermodel_identity_patch": supermodel_identity_patch,
        "body_overrides": body_outputs,
        "r3a": extraction.to_dict() if extraction is not None else None,
        "r3b": writer_result.to_dict() if writer_result is not None else None,
        "raw_duplicate": raw_duplicate,
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
    parser.add_argument("--supermodel-resref", default=DEFAULT_SUPERMODEL_RESREF)
    parser.add_argument("--animation-name", default="kpmwin1")
    parser.add_argument("--frame-step", type=int, default=1)
    parser.add_argument(
        "--clone-only-supermodel",
        action="store_true",
        help="Create a renamed S_Female02 clone without appending the custom animation.",
    )
    parser.add_argument(
        "--duplicate-source-animation",
        default="",
        help=(
            "Append a byte-reader-backed copy of an existing source supermodel animation under --animation-name. "
            "Useful for isolating append layout from retargeted controller payloads, e.g. f2a2 -> kpmwin1."
        ),
    )
    return parser.parse_args()


if __name__ == "__main__":
    raise SystemExit(build_assets(parse_args()))

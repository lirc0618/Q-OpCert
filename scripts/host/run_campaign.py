#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TERMINAL_CAPTURE_STATUSES = {"COMPLETE", "HARDFAULT", "HANG"}


def existing_capture_status(out: Path, binary_stem: str) -> str | None:
    status_files = sorted(out.glob(f"{binary_stem}_*.status.json"))
    for path in reversed(status_files):
        try:
            status = json.loads(path.read_text(encoding="utf-8"))["status"]
        except (OSError, KeyError, json.JSONDecodeError):
            continue
        if status in TERMINAL_CAPTURE_STATUSES:
            return status
    return None


def select_ids(items: list[dict[str, object]], requested: list[str]) -> list[dict[str, object]]:
    by_id = {str(item["mutant_id"]): item for item in items}
    unknown = [mutant_id for mutant_id in requested if mutant_id not in by_id]
    if unknown:
        raise ValueError(f"mutant IDs not present in manifest: {', '.join(unknown)}")
    return [by_id[mutant_id] for mutant_id in requested]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--manifest", type=Path, default=ROOT / "results" / "firmware_manifest.json")
    p.add_argument("--port", required=True)
    p.add_argument("--backend", choices=["stm32cli", "st-flash", "openocd", "stlinkcli"], default="stm32cli")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--timeout", type=float, default=20.0)
    p.add_argument("--out", type=Path, default=ROOT / "board_results")
    p.add_argument("--only", choices=["all", "reference", "mutants"], default="all")
    p.add_argument("--ids", nargs="+", metavar="IS-NNNN",
                   help="run these mutant IDs in the given order")
    p.add_argument("--force", action="store_true")
    args = p.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    items = json.loads(args.manifest.read_text())
    if args.only == "reference":
        items = [x for x in items if x["mutant_id"] == "NONE"]
    elif args.only == "mutants":
        items = [x for x in items if x["mutant_id"] != "NONE"]
    if args.ids:
        try:
            items = select_ids(items, args.ids)
        except ValueError as exc:
            p.error(str(exc))

    status_path = args.out / "campaign_status.csv"
    rows = []
    for index, item in enumerate(items, start=1):
        name = item["name"]
        binary = ROOT / item["bin"]
        existing_status = existing_capture_status(args.out, binary.stem)
        if existing_status and not args.force:
            rows.append({"name": name, "status": f"skipped_{existing_status.lower()}", "returncode": 0, "timestamp_utc": datetime.now(timezone.utc).isoformat()})
            print(f"[{index}/{len(items)}] skip {name} ({existing_status})")
            continue
        cmd = [
            sys.executable,
            str(ROOT / "host" / "flash_and_capture.py"),
            str(binary),
            "--port", args.port,
            "--backend", args.backend,
            "--baud", str(args.baud),
            "--timeout", str(args.timeout),
            "--out", str(args.out),
        ]
        print(f"[{index}/{len(items)}] run {name}")
        result = subprocess.run(cmd)
        capture_status = existing_capture_status(args.out, binary.stem)
        rows.append({
            "name": name,
            "status": capture_status.lower() if capture_status else ("ok" if result.returncode == 0 else "failed"),
            "returncode": result.returncode,
            "timestamp_utc": datetime.now(timezone.utc).isoformat(),
        })
        with status_path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=["name", "status", "returncode", "timestamp_utc"])
            writer.writeheader()
            writer.writerows(rows)
        if result.returncode != 0 and capture_status != "HANG":
            print(f"campaign stopped at {name}; rerun to resume", file=sys.stderr)
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

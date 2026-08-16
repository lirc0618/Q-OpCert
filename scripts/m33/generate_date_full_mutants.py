from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PHASE = ROOT.parent
DENOMINATOR = PHASE / "board_results" / "m33" / "date_fault_sites" / "DATE-M33-instruction-site-denominator.csv"
FLASH_BASE = 0x08000000


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("p0", "p1", "p4"), default="p0")
    args = parser.parse_args()
    firmware = f"m33_cmsis_hardened_{args.profile}"
    source = ROOT / firmware / "build" / f"{firmware}.bin"
    out = ROOT / "date_full_mutants" / args.profile
    out.mkdir(parents=True, exist_ok=True)
    clean = source.read_bytes()
    clean_sha = sha256(clean)
    with DENOMINATOR.open(newline="", encoding="utf-8") as handle:
        rows = [row for row in csv.DictReader(handle) if row["profile"] == args.profile]
    manifests: list[dict[str, object]] = []
    for row in rows:
        offset = int(row["address"], 16) - FLASH_BASE
        original = bytes.fromhex(row["original"])
        replacement = bytes.fromhex(row["replacement"])
        if clean[offset:offset + len(original)] != original:
            raise RuntimeError(f"source mismatch at {row['site_id']} {row['address']}")
        image = bytearray(clean)
        image[offset:offset + len(original)] = replacement
        image_bytes = bytes(image)
        binary = out / f"{row['site_id']}.bin"
        binary.write_bytes(image_bytes)
        manifests.append({
            **row, "binary": str(binary), "source": str(source),
            "source_sha256": clean_sha, "sha256": sha256(image_bytes),
        })
    manifest = out / f"DATE-M33-{args.profile}-full-mutants.json"
    manifest.write_text(json.dumps(manifests, indent=2) + "\n", encoding="utf-8")
    print(f"profile={args.profile} mutants={len(manifests)} manifest={manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

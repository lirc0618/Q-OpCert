from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PHASE = ROOT.parent
CAPTURE = PHASE / "host" / "flash_and_capture.py"


def classify(item: dict[str, object], status_path: Path, exit_code: int) -> dict[str, object]:
    status = json.loads(status_path.read_text(encoding="utf-8"))
    csv_path = status_path.with_name(status_path.name.removesuffix(".status.json") + ".csv")
    rows: list[dict[str, str]] = []
    if csv_path.is_file():
        with csv_path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
    erroneous = sum(int(row["max_abs_error"], 0) > 0 for row in rows)
    rejected = sum(int(row["accepted"], 0) == 0 for row in rows)
    bounded_accept = sum(
        int(row["accepted"], 0) == 1 and 0 < int(row["max_abs_error"], 0) <= 2 for row in rows
    )
    silent_violation = sum(
        int(row["accepted"], 0) == 1 and int(row["max_abs_error"], 0) > 2 for row in rows
    )
    return {
        "site_id": item["site_id"], "profile": item["profile"],
        "function": item["function"], "function_category": item["function_category"],
        "instruction_category": item["instruction_category"], "address": item["address"],
        "mnemonic": item["mnemonic"], "capture_status": status["status"],
        "capture_exit": exit_code, "rows": len(rows), "erroneous": erroneous,
        "rejected": rejected, "bounded_accept": bounded_accept,
        "silent_violation": silent_violation, "status_file": str(status_path),
    }


def write_summary(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=("p0", "p1", "p4"), default="p0")
    parser.add_argument("--flash", action="store_true")
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--start", type=int, default=1, help="One-based inclusive site index.")
    parser.add_argument("--count", type=int, default=40)
    parser.add_argument("--skip", default="", help="Comma-separated one-based site indices to skip (known blind sites).")
    args = parser.parse_args()
    if not args.flash:
        parser.error("--flash is required")
    manifest = ROOT / "date_full_mutants" / args.profile / f"DATE-M33-{args.profile}-full-mutants.json"
    items = json.loads(manifest.read_text(encoding="utf-8"))
    skip_set = {int(x) for x in args.skip.split(",") if x.strip()}
    selected = [
        item for idx, item in enumerate(items, 1)
        if args.start <= idx < args.start + args.count and idx not in skip_set
    ]
    out = PHASE / "board_results" / "m33" / "date_fault_full" / args.profile
    out.mkdir(parents=True, exist_ok=True)
    batch = out / f"batch_{args.start:04d}_{args.start + len(selected) - 1:04d}.csv"
    programmer = Path(r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin")
    env = os.environ.copy()
    env["PATH"] = str(programmer) + os.pathsep + env.get("PATH", "")
    results: list[dict[str, object]] = []
    for sequence, item in enumerate(selected, args.start):
        binary = Path(item["binary"])
        print(f"[{sequence}/{len(items)}] {item['site_id']} {item['function']} {item['address']}", flush=True)
        process = subprocess.run(
            [sys.executable, str(CAPTURE), str(binary), "--port", args.port,
             "--backend", "stm32cli", "--timeout", str(args.timeout), "--out", str(out)],
            cwd=PHASE, env=env,
        )
        matches = sorted(out.glob(f"{binary.stem}_{item['sha256'][:12]}*.status.json"),
                         key=lambda path: path.stat().st_mtime_ns)
        if not matches:
            raise RuntimeError(f"missing status for {item['site_id']}")
        results.append(classify(item, matches[-1], process.returncode))
        write_summary(batch, results)
        if results[-1]["silent_violation"]:
            print(f"STOP silent violation at {item['site_id']}", flush=True)
            return 3
    print(f"batch={batch} completed={len(results)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

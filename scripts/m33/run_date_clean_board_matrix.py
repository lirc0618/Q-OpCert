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
PROFILES = ("p0", "p1", "p2", "p3", "p4")
VARIANTS = ("baseline", "shadow", "exact", "abft", "hardened")


def newest_status(out: Path, stem: str) -> Path:
    matches = list(out.glob(f"{stem}_*.status.json"))
    if not matches:
        raise RuntimeError(f"capture status missing for {stem}")
    return max(matches, key=lambda path: path.stat().st_mtime_ns)


def validate_capture(status_path: Path, variant: str) -> dict[str, object]:
    status = json.loads(status_path.read_text(encoding="utf-8"))
    csv_path = status_path.with_name(status_path.name.removesuffix(".status.json") + ".csv")
    with csv_path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    protected = variant != "baseline"
    clean = (
        status["status"] == "COMPLETE"
        and len(rows) == 128
        and all(int(row["status"], 0) == 0 for row in rows)
        and all(int(row["syndrome"], 0) == 0 for row in rows)
        and all(int(row["accepted"], 0) == 1 for row in rows)
        and all(int(row["max_abs_error"], 0) == 0 for row in rows)
        and all(int(row["crash_magic"], 0) == 0 for row in rows)
        and all(int(row["crash_count"], 0) == 0 for row in rows)
    )
    check_counts = sorted({int(row["checks"], 0) for row in rows})
    abft_counts = sorted({int(row["abft_checks"], 0) for row in rows})
    if protected:
        clean &= check_counts != [0]
    if variant in ("abft", "hardened"):
        clean &= abft_counts != [0]
    return {
        "capture_status": status["status"],
        "rows": len(rows),
        "check_counts": ";".join(map(str, check_counts)),
        "abft_check_counts": ";".join(map(str, abft_counts)),
        "max_cycles": max(int(row["cycles"], 0) for row in rows),
        "min_cycles": min(int(row["cycles"], 0) for row in rows),
        "clean_gate": bool(clean),
        "status_file": str(status_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Flash, capture, and validate the DATE M33 clean matrix.")
    parser.add_argument("--flash", action="store_true", help="Required acknowledgement for repeated board programming.")
    parser.add_argument("--port", default="COM6")
    parser.add_argument("--timeout", type=float, default=25.0)
    parser.add_argument("--profiles", nargs="+", choices=PROFILES, default=list(PROFILES))
    parser.add_argument("--variants", nargs="+", choices=VARIANTS, default=list(VARIANTS))
    args = parser.parse_args()
    if not args.flash:
        parser.error("--flash is required; no board access was performed")

    programmer = Path(r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin")
    env = os.environ.copy()
    env["PATH"] = str(programmer) + os.pathsep + env.get("PATH", "")
    out = PHASE / "board_results" / "m33" / "date_clean"
    out.mkdir(parents=True, exist_ok=True)
    summary: list[dict[str, object]] = []

    for profile in args.profiles:
        for variant in args.variants:
            firmware = f"m33_cmsis_{variant}_{profile}"
            binary = ROOT / firmware / "build" / f"{firmware}.bin"
            if not binary.is_file():
                raise RuntimeError(f"missing binary: {binary}")
            print(f"FLASH/CAPTURE {profile}/{variant}", flush=True)
            subprocess.run(
                [sys.executable, str(CAPTURE), str(binary), "--port", args.port,
                 "--backend", "stm32cli", "--timeout", str(args.timeout), "--out", str(out)],
                cwd=PHASE, env=env, check=True,
            )
            status_path = newest_status(out, firmware)
            result = {"profile": profile, "variant": variant, **validate_capture(status_path, variant)}
            summary.append(result)
            if not result["clean_gate"]:
                print(f"CLEAN GATE FAILED: {result}", flush=True)
                break
        if summary and not summary[-1]["clean_gate"]:
            break

    summary_path = out / "DATE-M33-clean-board-matrix.csv"
    with summary_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    failures = [row for row in summary if not row["clean_gate"]]
    print(f"summary={summary_path}")
    print(f"passed={len(summary) - len(failures)}/{len(summary)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

import csv
import hashlib
import json
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PHASE = ROOT.parent
CAPTURES = PHASE / "board_results" / "m33" / "date_clean"
PROFILES = ("p0", "p1", "p2", "p3", "p4")
VARIANTS = ("baseline", "shadow", "exact", "abft", "hardened")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    digest.update(path.read_bytes())
    return digest.hexdigest()


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def find_capture(firmware: str, digest: str) -> tuple[Path, dict[str, object]]:
    matches = sorted(CAPTURES.glob(f"{firmware}_{digest[:12]}*.status.json"))
    if not matches:
        raise RuntimeError(f"no capture matches current binary {firmware} {digest}")
    complete: list[tuple[Path, dict[str, object]]] = []
    for path in matches:
        payload = json.loads(path.read_text(encoding="utf-8"))
        if payload.get("firmware_sha256") == digest and payload.get("status") == "COMPLETE":
            complete.append((path, payload))
    if not complete:
        raise RuntimeError(f"no complete capture matches current binary {firmware} {digest}")
    return max(complete, key=lambda item: item[0].stat().st_mtime_ns)


def main() -> int:
    rows: list[dict[str, object]] = []
    medians: dict[tuple[str, str], float] = {}
    for profile in PROFILES:
        for variant in VARIANTS:
            firmware = f"m33_cmsis_{variant}_{profile}"
            binary = ROOT / firmware / "build" / f"{firmware}.bin"
            digest = sha256(binary)
            status_path, status = find_capture(firmware, digest)
            csv_path = status_path.with_name(status_path.name.removesuffix(".status.json") + ".csv")
            with csv_path.open(newline="", encoding="utf-8") as handle:
                trials = list(csv.DictReader(handle))
            cycles = [int(row["cycles"], 0) for row in trials]
            clean = (
                len(trials) == 128
                and all(int(row["status"], 0) == 0 for row in trials)
                and all(int(row["syndrome"], 0) == 0 for row in trials)
                and all(int(row["accepted"], 0) == 1 for row in trials)
                and all(int(row["max_abs_error"], 0) == 0 for row in trials)
                and all(int(row["crash_magic"], 0) == 0 for row in trials)
                and all(int(row["crash_count"], 0) == 0 for row in trials)
            )
            median = float(statistics.median(cycles))
            medians[(profile, variant)] = median
            rows.append({
                "profile": profile, "variant": variant, "firmware_sha256": digest,
                "trials": len(trials), "median_cycles": median,
                "q1_cycles": percentile(cycles, 0.25), "q3_cycles": percentile(cycles, 0.75),
                "min_cycles": min(cycles), "max_cycles": max(cycles),
                "clean_gate": clean, "capture": str(status_path),
            })

    for row in rows:
        baseline = medians[(str(row["profile"]), "baseline")]
        row["overhead_percent"] = (float(row["median_cycles"]) / baseline - 1.0) * 100.0
    out = CAPTURES / "DATE-M33-clean-board-all-profiles.csv"
    with out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    failures = [row for row in rows if not row["clean_gate"]]
    print(f"summary={out}")
    print(f"passed={len(rows) - len(failures)}/{len(rows)}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

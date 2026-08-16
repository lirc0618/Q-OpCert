from __future__ import annotations

import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CLEAN = {
    "baseline": (ROOT / "M33-01-BASE.txt", 0, 0),
    "shadow": (ROOT / "M33-02-SHADOW.txt", 8, 0),
    "exact": (ROOT / "M33-03-EXACT.txt", 8, 0),
    "abft": (ROOT / "M33-05-ABFT.txt", 8, 2),
    "hardened": (ROOT / "M33-06-HARD.txt", 8, 2),
}
FAULTS = [ROOT / f"M33-{kind}-{index:02d}.txt" for kind, count in (("AF", 2), ("HR", 4)) for index in range(1, count + 1)]


def rows(path: Path) -> list[dict[str, str]]:
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    start = next(i for i, line in enumerate(lines) if "firmware_id," in line)
    lines[start] = lines[start][lines[start].index("firmware_id,"):]
    end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith("END,"))
    return list(csv.DictReader(lines[start:end]))


def main() -> int:
    clean_rows: dict[str, list[dict[str, str]]] = {}
    for name, (path, expected_checks, expected_abft) in CLEAN.items():
        data = rows(path)
        assert len(data) == 128 and {int(r["trial"]) for r in data} == set(range(128)), name
        assert all(int(r["status"]) == 0 and int(r["syndrome"]) == 0 for r in data), name
        assert all(int(r["checks"]) == expected_checks for r in data), name
        assert all(int(r["abft_syndrome"]) == 0 and int(r["abft_checks"]) == expected_abft for r in data), name
        assert all(int(r["accepted"]) == 1 and int(r["max_abs_error"]) == 0 for r in data), name
        clean_rows[name] = data
    reference = [r["checksum"] for r in clean_rows["baseline"]]
    for name, data in clean_rows.items():
        assert [r["checksum"] for r in data] == reference, f"checksum mismatch: {name}"

    for path in FAULTS:
        data = rows(path)
        assert len(data) == 128 and {int(r["trial"]) for r in data} == set(range(128)), path.name
        assert all(int(r["max_abs_error"]) > 0 for r in data), f"not activated: {path.name}"
        assert all(int(r["accepted"]) == 0 for r in data), f"not rejected: {path.name}"
        assert all(int(r["syndrome"]) != 0 or int(r["abft_syndrome"]) != 0 for r in data), f"no warning: {path.name}"
    print("M33 matrix validation PASS: 5 clean variants + 6 fault regressions, 128 trials each")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Portable verification of the Q-OpCert DATE 2027 artifact."""

from __future__ import annotations

import csv
import hashlib
import statistics
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
PROFILES = ("p0", "p1", "p4")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as stream:
        return list(csv.DictReader(stream))


def integer(row: dict[str, str], field: str) -> int:
    return int(row[field])


def main() -> None:
    denominator = read_csv(DATA / "site_denominator" / "DATE-M33-instruction-site-denominator.csv")
    assert len(denominator) == 1971
    assert Counter(row["profile"] for row in denominator) == Counter(
        {profile: 657 for profile in PROFILES}
    )

    expected = {
        "p0": (598, 3, 56, 76655),
        "p1": (596, 3, 58, 76335),
        "p4": (597, 3, 57, 76535),
    }
    all_rows: list[dict[str, str]] = []
    for profile in PROFILES:
        rows = read_csv(DATA / "final_campaign" / profile / "summary.csv")
        assert len(rows) == 657
        status = Counter(row["capture_status"] for row in rows)
        observed = (
            status["COMPLETE"],
            status["PARTIAL"],
            status["HANG"],
            sum(integer(row, "rows") for row in rows),
        )
        assert observed == expected[profile], (profile, observed)
        assert sum(integer(row, "silent_violation") for row in rows) == 0
        for row in rows:
            assert (ROOT / row["status_file"]).is_file(), row["status_file"]
        all_rows.extend(rows)

    totals = {
        field: sum(integer(row, field) for row in all_rows)
        for field in ("rows", "erroneous", "rejected", "bounded_accept", "silent_violation")
    }
    assert totals == {
        "rows": 229525,
        "erroneous": 99111,
        "rejected": 85239,
        "bounded_accept": 14256,
        "silent_violation": 0,
    }, totals

    p0 = read_csv(DATA / "final_campaign" / "p0" / "summary.csv")
    no_op = next(row for row in p0 if row["site_id"] == "M33-P0-IS-0657")
    status_path = ROOT / no_op["status_file"]
    trials = read_csv(status_path.with_suffix("").with_suffix(".csv"))
    assert statistics.median(int(row["cycles"]) for row in trials) == 7601
    binary = ROOT / "binaries" / "p0" / "M33-P0-IS-0657.bin"
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == (
        "6b505ed6439423bd82c98f43990454d77cab045dbfd19b6a4362904fb416227a"
    )
    assert round((7601 - 5277) * 100 / 5277, 3) == 44.040

    print("DATE artifact verification: PASS")
    for key, value in totals.items():
        print(f"{key}={value}")
    print("final_p0_median_cycles=7601")
    print("final_p0_overhead_percent=44.040")


if __name__ == "__main__":
    main()


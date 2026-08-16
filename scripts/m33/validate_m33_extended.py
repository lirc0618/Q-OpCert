from __future__ import annotations

import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "board_results" / "m33"
IDS = [f"M33-AF-{index:02d}" for index in range(3, 7)]


def newest_status(mutant_id: str) -> Path:
    matches = sorted(RESULTS.glob(f"{mutant_id}_*.status.json"), key=lambda p: p.stat().st_mtime)
    if not matches:
        raise FileNotFoundError(f"missing status for {mutant_id}")
    return matches[-1]


def load_rows(status_path: Path) -> list[dict[str, str]]:
    csv_path = status_path.with_suffix("").with_suffix(".csv")
    if not csv_path.exists():
        raise FileNotFoundError(csv_path)
    with csv_path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def main() -> int:
    summary: list[dict[str, object]] = []
    for mutant_id in IDS:
        status_path = newest_status(mutant_id)
        status = json.loads(status_path.read_text(encoding="utf-8"))
        rows = load_rows(status_path)
        if status.get("status") != "COMPLETE" or len(rows) != 128:
            raise AssertionError(f"{mutant_id}: incomplete capture")
        if {int(row["trial"]) for row in rows} != set(range(128)):
            raise AssertionError(f"{mutant_id}: trial set mismatch")

        activated = sum(int(row["max_abs_error"]) > 0 for row in rows)
        rejected = sum(int(row["accepted"]) == 0 for row in rows)
        warning = sum(
            int(row["syndrome"]) != 0 or int(row["abft_syndrome"]) != 0
            for row in rows
        )
        silent = sum(
            int(row["max_abs_error"]) > 0 and int(row["accepted"]) != 0
            for row in rows
        )
        masked = len(rows) - activated

        if mutant_id in {"M33-AF-03", "M33-AF-04"}:
            if (activated, rejected, warning, silent) != (128, 128, 128, 0):
                raise AssertionError(f"{mutant_id}: expected activated safe rejection")
            classification = "activated_rejected"
        else:
            if (activated, rejected, warning, silent) != (0, 0, 0, 0):
                raise AssertionError(f"{mutant_id}: expected masked control")
            classification = "masked_control"

        summary.append({
            "id": mutant_id,
            "rows": len(rows),
            "activated": activated,
            "rejected": rejected,
            "warning": warning,
            "masked": masked,
            "silent_accept": silent,
            "classification": classification,
        })

    output = RESULTS / "M33-fault-extended-summary.csv"
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    print(f"M33 extended validation PASS: {len(IDS)} images x 128 trials; {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

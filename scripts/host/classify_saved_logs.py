#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path

from flash_and_capture import CSV_HEADER, classify_lines


def main() -> int:
    parser = argparse.ArgumentParser(description="Classify saved UART logs without modifying raw data")
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--status-out", type=Path, required=True)
    parser.add_argument("--normalized-out", type=Path, required=True)
    args = parser.parse_args()

    status_rows: list[dict[str, object]] = []
    normalized_rows: list[list[str]] = []
    for path in args.inputs:
        raw = path.read_bytes()
        text = raw.decode("ascii", errors="ignore").replace("\x00", "")
        result, normalized = classify_lines(text.splitlines())
        if normalized:
            normalized_rows.extend(csv.reader(normalized[1:]))
        status_rows.append({
            "source_file": str(path),
            "source_bytes": len(raw),
            **result.__dict__,
        })

    args.status_out.parent.mkdir(parents=True, exist_ok=True)
    with args.status_out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=status_rows[0].keys())
        writer.writeheader()
        writer.writerows(status_rows)
    with args.normalized_out.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(CSV_HEADER.split(","))
        writer.writerows(normalized_rows)

    counts: dict[str, int] = {}
    for row in status_rows:
        status = str(row["status"])
        counts[status] = counts.get(status, 0) + 1
    print(" ".join(f"{key}={counts[key]}" for key in sorted(counts)))
    print(f"normalized_rows={len(normalized_rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

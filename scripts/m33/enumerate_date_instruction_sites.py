from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
FUNCTION_CATEGORIES = {
    "arm_nn_mat_mult_kernel_s8_s16": "main_mac_and_control",
    "qopcert_cmsis_fused_check": "requant_shadow_compare",
    "qopcert_cmsis_abft_check": "abft_checksum_and_control",
    "qopcert_cmsis_abft_latch_primary": "syndrome_primary_rail",
    "qopcert_cmsis_abft_latch_secondary": "syndrome_secondary_rail",
    "qopcert_cmsis_abft_syndrome": "syndrome_reduction",
    "qopcert_infective_commit4": "commit",
}
FUNCTION_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
INSTRUCTION_RE = re.compile(
    r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{4})(?:\s+[0-9a-f]{4})?)\s+([^\s]+)(?:\s+(.*))?$"
)


def classify_instruction(mnemonic: str, operands: str) -> str:
    mnemonic = mnemonic.lower()
    if mnemonic.startswith("b") or mnemonic in {"cbz", "cbnz"}:
        return "control_flow"
    if mnemonic.startswith(("ldr", "ldm")):
        return "load"
    if mnemonic.startswith(("str", "stm")):
        return "store"
    if mnemonic in {"smlad", "smlabb", "smlabt", "smlatb", "smlatt", "mla", "mul", "smmulr"}:
        return "arithmetic_mac"
    if mnemonic.startswith(("cmp", "cmn", "tst", "teq")):
        return "compare"
    if mnemonic.startswith(("push", "pop")):
        return "function_state"
    if "pc" in operands:
        return "control_flow"
    return "arithmetic_or_data"


def parse(disasm: str, profile: str) -> list[dict[str, object]]:
    current_function: str | None = None
    rows: list[dict[str, object]] = []
    counters: dict[str, int] = {}
    for line in disasm.splitlines():
        function_match = FUNCTION_RE.match(line)
        if function_match:
            current_function = function_match.group(2)
            continue
        if current_function not in FUNCTION_CATEGORIES:
            continue
        match = INSTRUCTION_RE.match(line)
        if not match:
            continue
        address = int(match.group(1), 16)
        halfwords = match.group(2).split()
        # GNU objdump renders each Thumb halfword as a host-readable 16-bit
        # value; the linked binary stores each halfword little-endian.
        raw = b"".join(bytes.fromhex(halfword)[::-1] for halfword in halfwords)
        mnemonic = match.group(3)
        operands = (match.group(4) or "").strip()
        counters[current_function] = counters.get(current_function, 0) + 1
        rows.append({
            "site_id": f"M33-{profile.upper()}-IS-{len(rows) + 1:04d}",
            "profile": profile,
            "function": current_function,
            "function_category": FUNCTION_CATEGORIES[current_function],
            "instruction_category": classify_instruction(mnemonic, operands),
            "function_index": counters[current_function],
            "address": f"0x{address:08x}",
            "width": len(raw),
            "original": raw.hex(),
            "replacement": "00bf" * (len(raw) // 2),
            "mnemonic": mnemonic,
            "operands": operands,
        })
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Enumerate the auditable M33 instruction-skip denominator.")
    parser.add_argument("--profiles", nargs="+", choices=("p0", "p1", "p2", "p3", "p4"),
                        default=["p0", "p1", "p4"])
    args = parser.parse_args()
    all_rows: list[dict[str, object]] = []
    summary: list[dict[str, object]] = []
    for profile in args.profiles:
        firmware = f"m33_cmsis_hardened_{profile}"
        build = ROOT / firmware / "build"
        disasm_path = build / f"{firmware}.disasm.txt"
        binary_path = build / f"{firmware}.bin"
        rows = parse(disasm_path.read_text(encoding="utf-8", errors="replace"), profile)
        if not rows:
            raise RuntimeError(f"no target instructions parsed for {profile}")
        binary = binary_path.read_bytes()
        for row in rows:
            offset = int(str(row["address"]), 16) - 0x08000000
            width = int(row["width"])
            actual = binary[offset:offset + width].hex()
            if actual != row["original"]:
                raise RuntimeError(
                    f"{profile} byte mismatch at {row['address']}: "
                    f"manifest={row['original']} binary={actual}"
                )
        all_rows.extend(rows)
        by_category: dict[str, int] = {}
        for row in rows:
            key = str(row["function_category"])
            by_category[key] = by_category.get(key, 0) + 1
        for category, count in sorted(by_category.items()):
            summary.append({"profile": profile, "function_category": category, "sites": count})

    result_dir = ROOT.parent / "board_results" / "m33" / "date_fault_sites"
    result_dir.mkdir(parents=True, exist_ok=True)
    csv_path = result_dir / "DATE-M33-instruction-site-denominator.csv"
    json_path = result_dir / "DATE-M33-instruction-site-denominator.json"
    summary_path = result_dir / "DATE-M33-instruction-site-summary.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(all_rows[0]))
        writer.writeheader()
        writer.writerows(all_rows)
    json_path.write_text(json.dumps(all_rows, indent=2) + "\n", encoding="utf-8")
    with summary_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    print(f"sites={len(all_rows)}")
    print(f"denominator={csv_path}")
    print(f"summary={summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

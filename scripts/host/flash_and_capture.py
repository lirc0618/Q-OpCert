#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Callable
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

CSV_HEADER = "firmware_id,mutant_id,mode,trial,cycles,syndrome,accepted,max_abs_error,crash_magic,crash_count"
EXPECTED_TRIALS = 128


@dataclass(frozen=True)
class CaptureResult:
    status: str
    rows: int
    unique_trials: int
    end_seen: bool
    header_seen: bool
    hardfault_seen: bool
    detail: str


def classify_lines(lines: list[str], expected_trials: int = EXPECTED_TRIALS) -> tuple[CaptureResult, list[str]]:
    """Classify UART output and return normalized CSV lines without END."""
    header_index = None
    normalized_header = CSV_HEADER
    header_fields: list[str] = []
    for index, line in enumerate(lines):
        offset = line.find("firmware_id,")
        candidate = line[offset:] if offset >= 0 else ""
        fields = candidate.split(",")
        if "trial" in fields and "crash_magic" in fields and "crash_count" in fields:
            header_index = index
            normalized_header = candidate
            header_fields = fields
            break
    if header_index is None:
        return CaptureResult("NO_BOOT", 0, 0, False, False, False,
                             "CSV header not received"), []

    data_lines: list[str] = []
    trials: list[int] = []
    hardfault_seen = False
    end_seen = False
    for line in lines[header_index + 1:]:
        if line.startswith("END,"):
            end_seen = True
            break
        try:
            values = next(csv.reader([line]))
            if len(values) != len(header_fields):
                continue
            trial = int(values[header_fields.index("trial")], 0)
            crash_magic = int(values[header_fields.index("crash_magic")], 0)
            crash_count = int(values[header_fields.index("crash_count")], 0)
        except (ValueError, csv.Error):
            continue
        trials.append(trial)
        hardfault_seen |= crash_magic == 0x514F5046 or crash_count > 0
        data_lines.append(line)

    unique_trials = len(set(trials))
    normalized = [normalized_header, *data_lines]
    if hardfault_seen:
        status = "HARDFAULT"
        detail = "HardFault marker observed"
    elif end_seen and len(data_lines) == expected_trials and unique_trials == expected_trials:
        status = "COMPLETE"
        detail = f"{expected_trials} unique trials and END received"
    elif not data_lines and not end_seen:
        status = "HANG"
        detail = "header received but no trial or END before timeout"
    else:
        status = "PARTIAL"
        detail = (f"received {len(data_lines)} rows/{unique_trials} unique trials; "
                  f"END={'yes' if end_seen else 'no'}")
    return CaptureResult(status, len(data_lines), unique_trials, end_seen, True,
                         hardfault_seen, detail), normalized

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def available_artifact_stem(out: Path, base: str) -> str:
    """Keep every capture attempt instead of overwriting an earlier artifact."""
    if not any(out.glob(f"{base}.*")):
        return base
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    candidate = f"{base}_{timestamp}"
    suffix = 1
    while any(out.glob(f"{candidate}.*")):
        suffix += 1
        candidate = f"{base}_{timestamp}_{suffix}"
    return candidate


def stlink_cli() -> str:
    exe = shutil.which("ST-LINK_CLI")
    if not exe:
        program_files_x86 = os.environ.get("ProgramFiles(x86)")
        if program_files_x86:
            candidate = Path(program_files_x86) / "STMicroelectronics" / "STM32 ST-LINK Utility" / "ST-LINK Utility" / "ST-LINK_CLI.exe"
            if candidate.is_file():
                exe = str(candidate)
    if not exe:
        raise RuntimeError("ST-LINK_CLI not found")
    return exe


def flash(binary: Path, backend: str) -> None:
    if backend == "stm32cli":
        exe = shutil.which("STM32_Programmer_CLI")
        if not exe:
            raise RuntimeError("STM32_Programmer_CLI not found")
        cmd = [exe, "-c", "port=SWD", "-w", str(binary), "0x08000000", "-v", "-rst"]
    elif backend == "st-flash":
        exe = shutil.which("st-flash")
        if not exe:
            raise RuntimeError("st-flash not found")
        cmd = [exe, "--reset", "write", str(binary), "0x08000000"]
    elif backend == "openocd":
        exe = shutil.which("openocd")
        if not exe:
            raise RuntimeError("openocd not found")
        cmd = [exe, "-f", "interface/stlink.cfg", "-f", "target/stm32f4x.cfg", "-c",
               f"program {binary} verify reset exit 0x08000000"]
    elif backend == "stlinkcli":
        exe = stlink_cli()
        cmd = [exe, "-c", "SWD", "-P", str(binary), "0x08000000",
               "-V", "after_programming"]
    else:
        raise ValueError(backend)
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def reset_and_run(backend: str) -> None:
    if backend == "stm32cli":
        exe = shutil.which("STM32_Programmer_CLI")
        if not exe:
            raise RuntimeError("STM32_Programmer_CLI not found")
        cmd = [exe, "-c", "port=SWD", "-rst"]
    elif backend == "stlinkcli":
        cmd = [stlink_cli(), "-c", "SWD", "-Rst", "-Run", "0x08000000"]
    else:
        return
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True)


def capture(port: str, baud: int, timeout: float, raw_log: Path, csv_out: Path,
            after_open: Callable[[], None] | None = None,
            serial_reset: bool = False) -> CaptureResult:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("Install pyserial: python -m pip install pyserial") from exc

    deadline = time.monotonic() + timeout
    lines: list[str] = []
    # The board's CH340 routes modem-control lines into reset/BOOT logic.
    # This board runs user Flash with DTR deasserted and RTS asserted.
    ser = serial.Serial(port=None, baudrate=baud, timeout=0.25)
    ser.port = port
    ser.dtr = False
    ser.rts = True
    ser.open()
    with ser:
        ser.reset_input_buffer()
        if serial_reset:
            print("Serial capture open; applying controlled DTR reset pulse.", flush=True)
            ser.dtr = True
            time.sleep(0.1)
            ser.dtr = False
        elif after_open:
            after_open()
        else:
            print("Serial capture open; press physical RESET.", flush=True)
        while time.monotonic() < deadline:
            line = ser.readline().decode("ascii", errors="ignore").strip()
            if not line:
                continue
            print(line)
            lines.append(line)
            if line.startswith("END,"):
                break
    raw_log.write_text("\n".join(lines) + "\n", encoding="utf-8")
    result, normalized_lines = classify_lines(lines)
    if normalized_lines:
        csv_out.write_text("\n".join(normalized_lines) + "\n", encoding="utf-8")
    return result


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("binary", type=Path)
    p.add_argument("--port", required=True)
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--backend", choices=["stm32cli", "st-flash", "openocd", "stlinkcli"], default="stm32cli")
    p.add_argument("--timeout", type=float, default=20.0)
    p.add_argument("--out", type=Path, default=Path("board_results"))
    p.add_argument("--skip-flash", action="store_true", help="capture an already-programmed target")
    p.add_argument("--manual-reset", action="store_true",
                   help="wait for a physical reset after opening the serial port")
    p.add_argument("--serial-reset", action="store_true",
                   help="pulse DTR after opening serial while keeping RTS asserted")
    args = p.parse_args()
    if args.manual_reset and args.serial_reset:
        p.error("--manual-reset and --serial-reset are mutually exclusive")
    args.out.mkdir(parents=True, exist_ok=True)
    digest = sha256(args.binary)
    stem = available_artifact_stem(args.out, f"{args.binary.stem}_{digest[:12]}")
    if not args.skip_flash:
        flash(args.binary, args.backend)
    start_target = None if (args.manual_reset or args.serial_reset) else lambda: reset_and_run(args.backend)
    result = capture(args.port, args.baud, args.timeout, args.out / f"{stem}.log",
                     args.out / f"{stem}.csv", start_target, serial_reset=args.serial_reset)
    status_payload = {**asdict(result), "firmware_sha256": digest, "binary": str(args.binary)}
    (args.out / f"{stem}.status.json").write_text(
        json.dumps(status_payload, indent=2) + "\n", encoding="utf-8")
    print(f"capture_status={result.status} rows={result.rows} detail={result.detail}")
    print(f"firmware_sha256={digest}")
    return 0 if result.status in {"COMPLETE", "HARDFAULT"} else 2


if __name__ == "__main__":
    raise SystemExit(main())

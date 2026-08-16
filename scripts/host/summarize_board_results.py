#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
import pandas as pd

p = argparse.ArgumentParser()
p.add_argument("inputs", nargs="+", type=Path)
p.add_argument("--out", type=Path, default=Path("board_summary.csv"))
args = p.parse_args()
df = pd.concat([pd.read_csv(x) for x in args.inputs], ignore_index=True)
summary = df.groupby(["firmware_id", "mutant_id", "mode"], dropna=False).agg(
    trials=("trial", "count"),
    cycles_mean=("cycles", "mean"),
    cycles_min=("cycles", "min"),
    cycles_max=("cycles", "max"),
    detected_rate=("syndrome", lambda s: (s != 0).mean()),
    accepted_rate=("accepted", "mean"),
    max_abs_error=("max_abs_error", "max"),
    crash_rate=("crash_magic", lambda s: (s == 0x514F5046).mean()),
).reset_index()
summary.to_csv(args.out, index=False)
print(summary.to_string(index=False))

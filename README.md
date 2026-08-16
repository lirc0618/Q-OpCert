# Q-OpCert DATE 2027 Anonymous Artifact

This artifact supports the paper "Q-OpCert: Bounded-Error Shadow and ABFT Checks for Instruction-Skip Resilience in CMSIS-NN." It contains the protected source, campaign tooling, final static-site denominator, final board records, cost summaries, and a portable verifier for the headline results.

Anonymous review URL: <https://anonymous.4open.science/r/Q-OpCert-BDC8>

## Quick verification

From the artifact root, run:

```bash
python scripts/verify_results.py
```

Expected output ends with:

```text
DATE artifact verification: PASS
rows=229525
erroneous=99111
rejected=85239
bounded_accept=14256
silent_violation=0
final_p0_median_cycles=7601
final_p0_overhead_percent=44.040
```

The verifier reads the packaged CSV files, checks the 1,971-site denominator, recomputes all profile and aggregate counts, checks the clean-equivalent p0 NOP mutant hash, and recomputes the final cycle overhead.

## Layout

- `paper/`: anonymous manuscript source, bibliography, and PDF.
- `source/firmware/`: Q-OpCert harness and checker source.
- `source/m33_project/`: STM32U575 bare-metal project files.
- `source/cmsis_nn/`: CMSIS-NN headers, source, license, and upstream revision record. Two convolution sources contain the Q-OpCert integration.
- `source/cmsis_core_include/`: CMSIS Core headers used by the build.
- `scripts/`: build, site-enumeration, mutant-generation, capture, campaign, and verification scripts.
- `data/site_denominator/`: auditable instruction-site universe and category summary.
- `data/final_campaign/<profile>/summary.csv`: one row per final site.
- `data/final_campaign/<profile>/raw/`: per-mutant UART CSV and sanitized capture status.
- `data/clean_and_regression/`: clean-build and focused compound-regression evidence.
- `data/cost_and_microkernel/`: STM32F407 cost and legacy microkernel result summaries.
- `binaries/p0/`: the clean-equivalent original-NOP mutant used by the portable verifier.
- `MANIFEST.sha256`: SHA-256 digest of every packaged file except the manifest itself.

## Final campaign scope

The final campaign covers three linked STM32U575 binaries (`p0`, `p1`, and `p4`). For each profile, the enumerator parses every instruction in seven named linked functions: the main matrix kernel, shadow check, ABFT check, both syndrome-rail latches, syndrome reduction, and infective commit. Each 16-bit instruction is replaced by one 16-bit NOP; each 32-bit instruction is replaced by two 16-bit NOPs, preserving all subsequent addresses.

Each mutant attempts 128 deterministic trials. Trial `t` sets input element `i` to `((17*i + 13*t + 5) mod 31) - 15`; fixed formulas generate the other parameters. `COMPLETE`, `PARTIAL`, and `HANG` remain separate availability outcomes. A silent violation is an accepted output whose maximum absolute error exceeds two LSB.

## Rebuilding and rerunning

The recorded build used GNU Arm Embedded tools distributed with STM32CubeIDE 2.2.0, STM32CubeU5 v1.9.0, STM32CubeProgrammer 2.23.0, CMSIS-NN revision `22080c68d040c98139e6cb1549473e3149735f4d`, Cortex-M33, `-O2 -g3`, hard-float FPv5, and `ARM_MATH_DSP`.

Hardware reruns require a NUCLEO-U575ZI-Q, SWD access, and the board/UART settings documented by the scripts. Local STM32Cube installation paths and the serial port may need adjustment. Run the scripts in this order:

1. `scripts/m33/build_m33_baseline.py`
2. `scripts/m33/enumerate_date_instruction_sites.py`
3. `scripts/m33/generate_date_full_mutants.py`
4. `scripts/m33/run_date_full_campaign.py`

The portable result verifier does not require the Arm toolchain or hardware.

## Interpretation limits

This is an exhaustive census of the enumerated static instruction sites for three linked binaries and a fixed deterministic input set. It is not exhaustive over inputs, physical injection outcomes, register corruptions, instruction replacements, or coordinated multi-instruction faults. HANG and PARTIAL records demonstrate availability loss, not safe recovery of a partly written output buffer.

## Anonymity and provenance

Local absolute paths have been removed from summaries and status records. Build caches, IDE metadata, temporary mutants, obsolete batches, and author-identifying material are excluded. File-level provenance is recorded in `MANIFEST.sha256`.

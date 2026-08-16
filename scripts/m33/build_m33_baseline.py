from __future__ import annotations

import hashlib
import argparse
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PHASE = ROOT.parent
PROJECT = ROOT / "m33_smoke_uart"
CUBE = Path.home() / "STM32Cube" / "Repository" / "STM32Cube_FW_U5_V1.9.0"
IDE = Path(r"C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE")
CMSIS_NN = PHASE / "third_party" / "CMSIS-NN"
CMSIS_6 = PHASE / "third_party" / "CMSIS_6"


def tool(name: str) -> Path:
    found = list(IDE.glob(f"plugins/**/{name}"))
    if not found:
        raise SystemExit(f"tool not found: {name}")
    return found[0]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--variant", choices=["baseline", "shadow", "exact", "abft", "hardened"], default="baseline")
    parser.add_argument("--profile", choices=["p0", "p1", "p2", "p3", "p4"], default="p0")
    args = parser.parse_args()
    profiles = {
        "p0": (4, 5, 8, 4, 4, 4),
        "p1": (5, 6, 8, 3, 3, 8),
        "p2": (4, 4, 16, 1, 1, 16),
        "p3": (5, 5, 32, 3, 3, 16),
        "p4": (5, 5, 8, 3, 3, 8),
    }
    input_h, input_w, input_c, kernel_h, kernel_w, output_c = profiles[args.profile]
    modes = {
        "baseline": (30, 0), "shadow": (31, 1), "exact": (32, 2),
        "abft": (33, 3), "hardened": (34, 4),
    }
    harness_mode, checker_mode = modes[args.variant]
    firmware = f"m33_cmsis_{args.variant}_{args.profile}"
    gcc, objcopy, objdump, size = (tool(n) for n in (
        "arm-none-eabi-gcc.exe", "arm-none-eabi-objcopy.exe",
        "arm-none-eabi-objdump.exe", "arm-none-eabi-size.exe"))
    out = ROOT / firmware / "build"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    device = CUBE / "Drivers" / "CMSIS" / "Device" / "ST" / "STM32U5xx"
    hal = CUBE / "Drivers" / "STM32U5xx_HAL_Driver"
    cmsis = CUBE / "Drivers" / "CMSIS"
    flags = [
        "-mcpu=cortex-m33", "-mthumb", "-mfpu=fpv5-sp-d16", "-mfloat-abi=hard",
        "-DSTM32U575xx", "-DUSE_HAL_DRIVER", "-DARM_MATH_DSP",
        f'-DQOPCERT_FIRMWARE_ID="{firmware}"', f"-DQOPCERT_FUSED_HARNESS_MODE={harness_mode}",
        f"-DQOPCERT_CMSIS_FUSED_MODE={checker_mode}",
        f"-DQOPCERT_INPUT_H={input_h}", f"-DQOPCERT_INPUT_W={input_w}",
        f"-DQOPCERT_INPUT_C={input_c}", f"-DQOPCERT_KERNEL_H={kernel_h}",
        f"-DQOPCERT_KERNEL_W={kernel_w}", f"-DQOPCERT_OUTPUT_C={output_c}",
        "-O2", "-g3",
        "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra",
        f"-I{PROJECT / 'include'}", f"-I{PHASE / 'firmware' / 'include'}",
        f"-I{hal / 'Inc'}", f"-I{device / 'Include'}", f"-I{cmsis / 'Core' / 'Include'}",
        f"-I{CMSIS_NN / 'Include'}", f"-I{CMSIS_6 / 'CMSIS' / 'Core' / 'Include'}",
    ]
    sources = [
        PHASE / "firmware" / "src" / "cmsis_nn_fused_main.c",
        PROJECT / "src" / "platform_m33.c", PROJECT / "src" / "qopcert_exact_m33.c",
        PROJECT / "src" / "stm32u5xx_it.c",
        PROJECT / "src" / "system_stm32u5xx.c", PROJECT / "src" / "startup_stm32u575zitx.s",
    ]
    sources += [hal / "Src" / n for n in [
        "stm32u5xx_hal.c", "stm32u5xx_hal_cortex.c", "stm32u5xx_hal_flash.c",
        "stm32u5xx_hal_flash_ex.c", "stm32u5xx_hal_gpio.c", "stm32u5xx_hal_icache.c",
        "stm32u5xx_hal_pwr.c", "stm32u5xx_hal_pwr_ex.c", "stm32u5xx_hal_rcc.c",
        "stm32u5xx_hal_rcc_ex.c", "stm32u5xx_hal_uart.c", "stm32u5xx_hal_uart_ex.c",
    ]]
    sources += [CMSIS_NN / "Source" / area / name for area, name in [
        ("ConvolutionFunctions", "arm_convolve_s8.c"),
        ("ConvolutionFunctions", "arm_nn_mat_mult_kernel_s8_s16.c"),
        ("ConvolutionFunctions", "arm_nn_mat_mult_kernel_row_offset_s8_s16.c"),
        ("NNSupportFunctions", "arm_s8_to_s16_unordered_with_offset.c"),
    ]]
    if checker_mode:
        sources.append(PHASE / "firmware" / "src" / "qopcert_cmsis_fused.c")
    objects: list[Path] = []
    for i, source in enumerate(sources):
        obj = out / f"{i:02d}_{source.stem}.o"
        run([str(gcc), *flags, "-c", str(source), "-o", str(obj)])
        objects.append(obj)
    elf = out / f"{firmware}.elf"
    map_path = out / f"{firmware}.map"
    run([str(gcc), *flags, *(str(o) for o in objects),
         f"-T{PROJECT / 'linker' / 'STM32U575ZITX_FLASH.ld'}", "-Wl,--gc-sections",
         f"-Wl,-Map={map_path}", "--specs=nosys.specs", "-o", str(elf)])
    binary = out / f"{firmware}.bin"
    run([str(objcopy), "-O", "binary", str(elf), str(binary)])
    (out / f"{firmware}.disasm.txt").write_text(
        subprocess.check_output([str(objdump), "-d", "-S", str(elf)], text=True), encoding="utf-8")
    run([str(size), str(elf)])
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    (out / "manifest.json").write_text(json.dumps({
        "firmware": firmware, "sha256": digest, "harness_mode": harness_mode,
        "profile": args.profile,
        "shape": {"input_h": input_h, "input_w": input_w, "input_c": input_c,
                  "kernel_h": kernel_h, "kernel_w": kernel_w, "output_c": output_c},
        "cmsis_nn_commit": "22080c68d040c98139e6cb1549473e3149735f4d", "cubeu5": "v1.9.0",
    }, indent=2) + "\n", encoding="utf-8")
    print(f"binary={binary}\nsha256={digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

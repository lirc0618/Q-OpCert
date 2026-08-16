## Material Passport

- Artifact: STM32F407 fused CMSIS-NN final validation
- Verification Status: ANALYZED
- Platform: STM32F407 / Cortex-M4 DSP
- CMSIS-NN: v7.0.0 (`22080c68d040c98139e6cb1549473e3149735f4d`)
- Layer: `4×5×8` input, `4×4×8` filter, `1×2×4` output
- Raw data: `../../cmsis_fused_baseline.txt`, `../../cmsis_fused_shadow.txt`, `../../cmsis_fused_exact.txt`
- Trials: 128 paired deterministic inputs per build
- Analysis date: 2026-08-12

# CMSIS-NN 内核融合实验最终结果

## 数据有效性

三份文件均包含 1 行表头、128 行 trial 和 1 行 `END`。trial 完整覆盖 0--127，共审计 384 条记录：

- 全部 `status=0`、`syndrome=0`、`accepted=1`、`max_abs_error=0`；
- baseline 全部 `checks=0`；
- shadow 和 exact 全部 `checks=8`，证明每个 trial 的 8 个输出均经过内核融合 hook；
- 全部 `crash_magic=0` 且 `crash_count=0`；
- 三种构建的逐 trial checksum 序列完全一致；
- 三份文件均以 `END,3783030010` 结束。

本批通过全部预设有效性门槛。

## Cycle 结果

| 构建 | Mean/median/min/max | 标准差 | 相对 baseline 增量 | 相对开销 |
|---|---:|---:|---:|---:|
| fused baseline | 43134 | 0 | 0 | 0% |
| fused shadow | 43901 | 0 | +767 | +1.778% |
| fused exact | 43989 | 0 | +855 | +1.982% |

shadow 比 exact 少 88 cycles，即其保护增量比 exact-recompute 增量低 10.292%。这里的“标准差为零”只描述本确定性裸机测量条件，不应外推为不同板卡、温度或工具链下不存在变异。

## Flash 与静态 RAM

| 构建 | Flash text | 相对 baseline | BSS/RAM | 相对 baseline |
|---|---:|---:|---:|---:|
| fused baseline | 5304 B | 0 | 1260 B | 0 |
| fused shadow | 5798 B | +494 B (+9.314%) | 1268 B | +8 B (+0.635%) |
| fused exact | 5765 B | +461 B (+8.692%) | 1268 B | +8 B (+0.635%) |

shadow 的 Flash 略大于 exact，但运行 cycle 更少；静态代码尺寸与动态执行指令数不是同一指标。

## 可支持的结论

在 STM32F407 的 Cortex-M4 DSP 路径上，官方 `arm_convolve_s8` 已真实执行 `arm_nn_mat_mult_kernel_s8_s16`。融合 checker 在每个 trial 中直接消费内核 accumulator，并完成 8 次检查。对于该层形状，shadow 和 exact 保护的实测 cycle 开销分别为 1.778% 和 1.982%，无故障输出与 baseline 完全一致。

## 结论边界

本轮 checker 与主计算共享 accumulator，保护重点是 requantization、clamp、输出比较和 infective commit。它不能独立检测所有发生在 MAC 或 accumulator 形成过程中的共同故障。因此本结果不能单独支持“完整卷积数据通路均受保护”的主张。若论文要覆盖 MAC 数据通路，下一实现必须生成独立算术摘要或冗余 accumulator，并开展针对该路径的 fault campaign。

本报告是 F407 阶段快照，因此不包含能耗或 M33 板上数据。M33 结果现已完成，见 `../M33_BOARD_VALIDATION_2026-08-13_ZH.md`；能耗仍待外部测量。

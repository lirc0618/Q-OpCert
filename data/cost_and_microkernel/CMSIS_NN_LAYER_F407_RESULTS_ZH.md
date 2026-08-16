## Material Passport

- Artifact: STM32F407 CMSIS-NN layer validation report
- Status: ANALYZED
- Platform: STM32F407 / Cortex-M4
- CMSIS-NN: v7.0.0 (`22080c68d040c98139e6cb1549473e3149735f4d`)
- Raw data: `../../cmsis_layer_baseline.txt`, `../../cmsis_layer_shadow.txt`, `../../cmsis_layer_full.txt`
- Trials: 128 paired deterministic inputs per build
- Analysis date: 2026-08-12

# CMSIS-NN 真实卷积层实验结果

> 后续构建审计发现，本轮编译未显式启用 `ARM_MATH_DSP`，且 `1×1×4` 输出走的是 leftover 路径。因此本报告保留为功能接入证据，不作为最终 Cortex-M4 优化内核性能结果。最终性能以 `CMSIS_NN_FUSED_EXPERIMENT_ZH.md` 所定义的新一轮实验为准。

## 数据有效性

三份串口文件均有 1 行表头、128 行数据和 1 行 `END`，trial 完整覆盖 0--127。全部 384 条记录满足：

- `status=0`
- `max_abs_error=0`
- `syndrome=0`
- `accepted=1`
- `crash_magic=0` 且 `crash_count=0`

三个构建在对应 trial 上的输出 checksum 序列完全相同，最终均为 `END,3309100547`。本批数据通过预设有效性门槛。

## Cycle 结果

| 构建 | Mean/median/min/max (cycles) | 相对 baseline 增量 | 相对开销 |
|---|---:|---:|---:|
| baseline | 33204 | 0 | 0% |
| shadow | 33467 | +263 | +0.792% |
| full | 33511 | +307 | +0.925% |

每种构建的 128 次 cycle 读数完全一致，population standard deviation 为 0。shadow 比 full 少 44 cycles。

## Flash 与静态 RAM

| 构建 | Flash text | 相对 baseline | BSS/RAM | 相对 baseline |
|---|---:|---:|---:|---:|
| baseline | 5217 B | 0 | 1220 B | 0 |
| shadow | 5455 B | +238 B (+4.562%) | 1236 B | +16 B (+1.311%) |
| full | 5421 B | +204 B (+3.910%) | 1236 B | +16 B (+1.311%) |

shadow 的静态代码尺寸略大于 full 是链接后机器码结构造成的结果，并不与其运行 cycle 更少矛盾。

## 可支持的结论

这组实验直接支持：官方 `arm_convolve_s8` 能在 STM32F407 上与外部 shadow/exact 输出校验器组合；在本固定层形状和当前计时边界内，额外校验路径的实测增量分别为 263 和 307 cycles，且无故障运行的输出保持一致。

## 结论边界

累加值由独立参考循环在计时区间之前生成。因而上述百分比是“已有累加值时，卷积后校验与 commit 的增量”，不是包含参考累加值生成成本的完整端到端防护开销，也不是 CMSIS-NN 内部微内核融合实现的开销。论文不得用本结果单独声称完整防护仅增加 0.792%。

## 下一实验门槛

1. 将 shadow 校验所需的残余量或摘要在 `arm_nn_mat_mult_kernel_s8_s16` 内部随 MAC 同步生成，消除计时区间外的参考累加循环。
2. 对融合后的 baseline/shadow/full 重新采集 cycle、Flash、RAM。
3. 使用外部功耗测量设备，在相同供电、时钟和重复次数下采集能耗。
4. 在至少一个额外 Cortex-M 平台上复现实验，优先 Cortex-M7 或 Cortex-M33。

## Material Passport

- Artifact: STM32F407 CMSIS-NN shadow+ABFT clean-run validation
- Verification Status: ANALYZED
- Platform: STM32F407 / Cortex-M4 DSP
- Layer: `4×5×8 -> 1×2×4`
- Raw data: `../../cmsis_abft_baseline.txt`, `../../cmsis_fused_shadow_abft.txt`
- Trials: 128 paired deterministic inputs per build
- Analysis date: 2026-08-12

# CMSIS-NN MAC 路径 ABFT 最终无故障结果

## 有效性

两份文件均含表头、128个完整 trial 和 `END,3783030010`。共审计256条记录，全部满足：

- `status=0`、`syndrome=0`、`accepted=1`、`max_abs_error=0`；
- 无 `crash_magic` 或 `crash_count`；
- baseline 为 `checks=0,abft_checks=0`；
- shadow+ABFT 为 `checks=8,abft_checks=2,abft_syndrome=0`；
- 两个构建的逐 trial checksum 完全一致。

因此，ABFT 原型在本测试集上没有无故障误报，且独立 MAC 校验路径在每个 trial 中完整执行。

## Cycle

| 构建 | Mean/median/min/max | 标准差 | 增量 | 相对开销 |
|---|---:|---:|---:|---:|
| ABFT baseline | 43138 | 0 | 0 | 0% |
| fused shadow+ABFT | 45445 | 0 | +2307 | +5.348% |

与此前同层、同平台的输出路径融合结果结合，可形成两档设计点：

| 保护级别 | 保护范围 | 实测 cycle 开销 |
|---|---|---:|
| fused shadow | requantization、clamp、output comparison、commit | +1.778% |
| fused shadow+ABFT | 上述路径 + 独立通道校验点积覆盖主 MAC/accumulator | +5.348% |

两次实验的 baseline 分别为43134和43138 cycles，差4 cycles，原因是新增串口字段和 harness 代码布局；每个百分比均使用同一轮重新构建和烧录的对应 baseline，未跨轮混算。

## 静态开销

| 构建 | Flash text | BSS/RAM |
|---|---:|---:|
| ABFT baseline | 5410 B | 1260 B |
| fused shadow+ABFT | 6157 B | 1544 B |
| 增量 | +747 B (+13.808%) | +284 B (+22.540%) |

RAM 增量主要来自实验 harness 的128个 `int16` 校验权重（256 B）和状态。产品实现应把校验权重作为只读模型元数据放入 Flash；因此当前 BSS 是保守原型值，不代表优化部署的最终 RAM 开销。

## 可支持的结论与边界

该结果证明通道校验和 ABFT 能无误报地集成到本 CMSIS-NN DSP 卷积，并给出完整无故障开销。它尚未证明故障检测率。通道求和能够确定检测不发生抵消的单点 accumulator/MAC 误差，但多通道误差可能抵消，ABFT 路径自身也可能被故障攻击。检测率必须由下一轮针对性 instruction-skip campaign 给出。

## 下一门槛

针对以下位置生成故障版本并分别报告 injected、activated、detected、accepted-error、hang：

1. 主内核 `SMLAD` 和主 accumulator 更新；
2. 主 accumulator 通道求和；
3. ABFT 独立点积；
4. ABFT 比较和 syndrome 更新；
5. shadow checker 与 infective commit。

只有主 MAC 故障被激活后由 `abft_syndrome` 拒绝，才能把“覆盖 MAC 路径”写成板上故障检测证据。

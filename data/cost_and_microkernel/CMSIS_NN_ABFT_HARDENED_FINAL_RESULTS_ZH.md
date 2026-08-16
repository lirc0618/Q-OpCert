# 双轨粘滞 ABFT 硬化最终结果

## Material Passport

- Artifact: STM32F407 hardened ABFT clean run and compound-bypass regression
- Verification Status: ANALYZED
- Platform: STM32F407 / Cortex-M4 DSP
- Layer: CMSIS-NN `arm_convolve_s8`, `4×5×8 -> 1×2×4`
- Raw data: `../../HARD-CLEAN.txt`, `../../HR-01.txt`--`../../HR-04.txt`
- Trials: 128 clean + 4×128 compound-regression trials
- Analysis date: 2026-08-12

## 结论

双轨粘滞 syndrome 修复了旧单状态检查器在已测试复合跳过下的旁路问题。无故障运行保持逐 trial 数值正确；在主 MAC 已被破坏且 primary/secondary 比较调用或状态写回之一再被跳过的四种回归中，未受损 rail 均保留告警并阻止输出被接受。

| 固件 | 完整 trial | 非零 ABFT syndrome | 拒绝 | 静默接受错误 | cycle |
|---|---:|---:|---:|---:|---:|
| HARD-CLEAN | 128/128 | 0 | 0 | 0 | 45487 |
| HR-01 | 128/128 | 128 | 128 | 0 | 45487 |
| HR-02 | 128/128 | 128 | 128 | 0 | 45487 |
| HR-03 | 128/128 | 128 | 128 | 0 | 45497 |
| HR-04 | 128/128 | 128 | 128 | 0 | 45497 |
| HR 合计 | 512/512 | 512 | 512 | 0 | 45487--45497 |

所有文件均无 crash marker，HARD-CLEAN 的128个 checksum 与对应 baseline 一致。HR 中的 `max_abs_error=37--103` 是 infective commit 后的拒绝输出与 golden 的差异，不是被接受误差。

## 开销

| 构建 | cycles | 相对同轮 baseline | Flash text | BSS/RAM |
|---|---:|---:|---:|---:|
| ABFT baseline | 43138 | 0% | 5410 B | 1260 B |
| shadow+ABFT | 45445 | +5.348% | 6157 B | 1544 B |
| hardened shadow+ABFT | 45487 | +5.445% | 6230 B | 1548 B |

双轨硬化相对旧 ABFT 版本增加42 cycles（0.092%）、73 B Flash text和4 B BSS。实验原型仍把256 B校验权重放在BSS；产品实现可把只读校验元数据移入Flash。

## 证据边界

本实验是针对旧 CF-01/CF-02 已观察旁路机制的定向回归，证明单条 rail 的比较调用或状态写回被跳过时，另一条 rail 能阻止静默接受。它不等价于对所有双故障组合的完备证明，也不计入单故障检测率。结论仍限于一个固定层、一个编译配置和STM32F407；跨架构复现留待M33实板。

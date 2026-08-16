## Material Passport

- Artifact: targeted CMSIS-NN ABFT instruction-skip results
- Verification Status: ANALYZED
- Platform: STM32F407 / Cortex-M4 DSP
- Fault model: one static instruction replaced by equal-width NOP(s)
- Raw data: `../../AF-01.txt` through `../../AF-06.txt`
- Analysis date: 2026-08-12

# ABFT 单指令跳过实验结果

6个固件均输出128个trial和END，共768条记录；无hang、crash或不完整输出。

| ID | 目标 | Activated | Detected/rejected | Accepted error | Masked/not activated | 结论 |
|---|---|---:|---:|---:|---:|---|
| AF-01 | 主MAC `SMLAD`，列0 | 128 | 128 | 0 | 0 | `abft_syndrome=1`，全部拒绝 |
| AF-02 | 主MAC `SMLAD`，列1 | 128 | 128 | 0 | 0 | `abft_syndrome=2`，全部拒绝 |
| AF-03 | 主通道校验和更新 | 128 | 128 | 0 | 0 | `abft_syndrome=2`，全部拒绝 |
| AF-04 | ABFT独立点积 | 128 | 128 | 0 | 0 | 防护自身故障导致安全失败/误拒绝 |
| AF-05 | 第一列ABFT比较 | 0 | 0 | 0 | 128 | 干净主路径下未激活 |
| AF-06 | 第一列syndrome写回 | 0 | 0 | 0 | 128 | 干净主路径下未激活 |

对两个代表性主MAC跳过点，激活样本为256个，ABFT检测并拒绝256个，观察到的激活样本检测率为100%，接受错误为0。加入主通道校验和更新点后，三个主/校验数据路径点共384个激活样本均被拒绝。

这个100%只适用于所选静态位置、当前层、128个确定性输入和instruction-skip模型；不能外推为所有MAC指令、随机瞬态故障或物理注入的总体覆盖率。

AF-01至AF-04的`max_abs_error`为37--103，checksum也与clean不同。这是非零syndrome触发infective commit后输出被清零的预期结果，不是接受错误。统计必须以`accepted=0`记录为detected/rejected。

AF-04表明独立ABFT点积出错时系统安全拒绝，但会造成可用性损失。AF-05/06在没有主MAC错误时不激活，不能计为已检测或漏检。要验证它们能否旁路真实主错误，必须执行显式标注的双故障控制实验；该实验不属于单故障覆盖率。

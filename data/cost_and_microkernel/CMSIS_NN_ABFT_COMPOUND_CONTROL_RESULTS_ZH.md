## Material Passport

- Artifact: ABFT compound-fault bypass control results
- Verification Status: ANALYZED
- Platform: STM32F407 / Cortex-M4 DSP
- Fault model: two simultaneous static instruction skips
- Raw data: `../../CF-01.txt`, `../../CF-02.txt`
- Statistics scope: excluded from single-fault detection rate
- Analysis date: 2026-08-12

# ABFT 双故障旁路控制结果

## 结果

两份均完整输出128个trial和END，无hang或crash。

| ID | 组合 | ABFT syndrome | Accepted errors | 误差范围 | 中位数 | 平均值 |
|---|---|---:|---:|---:|---:|---:|
| CF-01 | 主MAC列0跳过 + 第一列比较跳过 | 0/128非零 | 128/128 | 1--47 LSB | 22 | 22.211 |
| CF-02 | 主MAC列0跳过 + 第一列syndrome写回跳过 | 0/128非零 | 128/128 | 1--47 LSB | 22 | 22.211 |

全部记录均为`checks=8,abft_checks=2,syndrome=0,abft_syndrome=0,accepted=1`。CF-01和CF-02的逐trial错误checksum序列完全相同，且与clean不同。

作为对照，AF-01单故障在相同128个输入上稳定产生`abft_syndrome=1`并全部拒绝。加入比较或syndrome写回跳过后，该证据被完全压制，确认两个位置均可旁路已激活的列0主MAC错误。

## 解释边界

这不是单故障检测率的一部分。单故障结果仍为两个代表性主MAC点256/256检测并拒绝。复合实验说明当前ABFT的比较与syndrome写回是防护关键点：当主数据错误和关键防护指令同时失效时，可产生静默接受错误。

不能由两个静态双跳过固件推导物理双故障概率，也不能声称所有复合故障均可旁路。

## 硬化方向

1. 对两个输出列使用独立、重复的比较与粘滞错误状态；
2. syndrome采用双轨编码，例如`error`与`~error`，commit同时验证互补关系；
3. commit不依赖单次普通store传递完整性状态；
4. 将重复比较版本加入单故障campaign，验证任一比较/store跳过仍能拒绝主错误；
5. 将双故障结果作为限制/攻击面实验，不作为主要检测率表的分子或分母。


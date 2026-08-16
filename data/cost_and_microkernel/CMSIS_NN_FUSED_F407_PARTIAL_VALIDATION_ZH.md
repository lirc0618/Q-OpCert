## Material Passport

- Artifact: STM32F407 fused CMSIS-NN partial validation
- Status: SUPERSEDED_BY_FINAL_REPORT
- Raw data: `../../cmsis_fused_baseline.txt`, `../../cmsis_fused_shadow.txt`, `../../cmsis_fused_exact.txt`
- Trials: 128 per build
- Analysis date: 2026-08-12

# 融合 CMSIS-NN 首轮板上结果

> shadow 已按 1-LSB 容差修复并重新烧录，最终结果见 `CMSIS_NN_FUSED_F407_FINAL_RESULTS_ZH.md`。本文件仅保留首次失效和修复过程的审计记录。

## 判定

- baseline：有效。128/128 trial 完整，`status=0`、`checks=0`、`max_abs_error=0`，cycles 恒为 43134。
- exact：有效。128/128 trial 完整，`status=0`、`checks=8`、`syndrome=0`、`accepted=1`、`max_abs_error=0`，cycles 恒为 43989。
- shadow：该次数据不纳入性能结论。128/128 trial 的 hook 均完整执行（`checks=8`），但 checker 错误采用零容差，将设计允许的 1-LSB shadow 差异判为故障；infective commit 随后按设计拒绝并破坏输出。

exact 相对 baseline 增加 855 cycles，即 1.982%。baseline 与 exact 的输出 checksum 序列完全一致。

## 根因与修复

Q-OpCert 的既有数学契约为 `|Q_exact-Q_shadow|<=1`，运行时接受条件应为 `|Q_observed-Q_shadow|<=1`。融合 helper 初版误写为严格相等。现已将 shadow 比较修复为 1-LSB 容差，exact 仍保持零容差。

修复没有改变 baseline 和 exact 的二进制；只需重新烧录：

`firmware/build/cmsis_fused_shadow/cmsis_fused_shadow.bin`

修复后 shadow SHA-256：

`2e16eb2d3cf2ecfc87d9e45db5af107224d622a89133a8347373b34513aa52e6`

重新采集文件仍命名为 `cmsis_fused_shadow.txt`。有效门槛为全部 128 行 `checks=8`、`syndrome=0`、`accepted=1`、`max_abs_error=0`。

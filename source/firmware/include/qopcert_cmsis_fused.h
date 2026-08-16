#ifndef QOPCERT_CMSIS_FUSED_H
#define QOPCERT_CMSIS_FUSED_H

#include <stdint.h>

void qopcert_cmsis_fused_reset(void);
void qopcert_cmsis_metadata_configure(const int32_t *multipliers,
                                      const int32_t *shifts,
                                      uint32_t output_channels,
                                      const int8_t *output_base,
                                      uint32_t output_count,
                                      int32_t output_offset,
                                      int32_t activation_min,
                                      int32_t activation_max);
void qopcert_cmsis_fused_check(int32_t accumulator,
                               const int8_t *out_ptr,
                               int8_t observed,
                               int32_t multiplier,
                               int32_t shift,
                               int32_t output_offset,
                               int32_t activation_min,
                               int32_t activation_max);
void qopcert_cmsis_return_pointer_check(const int8_t *returned, const int8_t *expected);
uint32_t qopcert_cmsis_fused_syndrome(void);
uint32_t qopcert_cmsis_fused_check_count(void);

void qopcert_cmsis_abft_configure(const int16_t *weight_checksum,
                                  int32_t bias_checksum,
                                  int32_t columns);
void qopcert_cmsis_abft_check(const int16_t *input_columns,
                              int32_t aligned_columns,
                              int32_t main_sum_0,
                              int32_t main_sum_1);
uint32_t qopcert_cmsis_abft_syndrome(void);
uint32_t qopcert_cmsis_abft_check_count(void);
void qopcert_cmsis_abft_latch_primary(int32_t check, int32_t main, uint32_t bit);
void qopcert_cmsis_abft_latch_secondary(int32_t check, int32_t main, uint32_t bit);

#endif

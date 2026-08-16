#ifndef QOPCERT_API_H
#define QOPCERT_API_H

#include <stdint.h>

int32_t qopcert_exact_requantize(int32_t val, int32_t multiplier, int32_t shift);
int32_t qopcert_shadow_requantize(int32_t val, int32_t multiplier, int32_t shift);
uint32_t qopcert_verify4_exact(const int32_t *acc,
                               const int8_t *observed,
                               const int32_t *multipliers,
                               const int32_t *shifts,
                               int32_t dst_offset,
                               int32_t activation_min,
                               int32_t activation_max);
uint32_t qopcert_verify4_shadow(const int32_t *acc,
                                const int8_t *observed,
                                const int32_t *multipliers,
                                const int32_t *shifts,
                                int32_t dst_offset,
                                int32_t activation_min,
                                int32_t activation_max);
uint32_t qopcert_infective_commit4(int8_t *output, uint32_t syndrome, uint32_t epoch);
void qopcert_epilogue4_exact(const int32_t *acc,
                             const int32_t *multipliers,
                             const int32_t *shifts,
                             int32_t dst_offset,
                             int32_t activation_min,
                             int32_t activation_max,
                             int8_t *output);
uint32_t qopcert_epilogue4_fused_shadow(const int32_t *acc,
                                        const int32_t *multipliers,
                                        const int32_t *shifts,
                                        int32_t dst_offset,
                                        int32_t activation_min,
                                        int32_t activation_max,
                                        int8_t *output);
void qopcert_microkernel_2x2_exact(const int8_t *lhs0,
                                   const int8_t *lhs1,
                                   const int8_t *rhs0,
                                   const int8_t *rhs1,
                                   int32_t cols,
                                   const int32_t *bias,
                                   const int32_t *multipliers,
                                   const int32_t *shifts,
                                   int32_t dst_offset,
                                   int8_t *dst);

#endif

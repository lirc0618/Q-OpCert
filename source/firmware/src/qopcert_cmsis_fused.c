#include "qopcert_cmsis_fused.h"
#include "qopcert_api.h"

#ifndef QOPCERT_CMSIS_FUSED_MODE
#error QOPCERT_CMSIS_FUSED_MODE must be 1 (shadow), 2 (exact), 3 (shadow+ABFT), or 4 (hardened shadow+ABFT)
#endif

static volatile uint32_t fused_syndrome;
static volatile uint32_t fused_checks;
static const int32_t *metadata_multipliers;
static const int32_t *metadata_shifts;
static uint32_t metadata_output_channels;
static int32_t metadata_output_offset;
static int32_t metadata_activation_min;
static int32_t metadata_activation_max;
static const int8_t *metadata_output_base;
static uint32_t metadata_output_count;
static const int16_t *abft_weights;
static int32_t abft_bias;
static int32_t abft_columns;
static volatile uint32_t abft_syndrome;
static volatile uint32_t abft_syndrome_inv;
static volatile uint32_t abft_checks;
static volatile uint32_t fused_return_fault;

static int32_t clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

void qopcert_cmsis_fused_reset(void)
{
    fused_syndrome = 0;
    fused_checks = 0;
    abft_syndrome = 0;
    abft_syndrome_inv = UINT32_MAX;
    abft_checks = 0;
    fused_return_fault = 0;
}

void qopcert_cmsis_metadata_configure(const int32_t *multipliers,
                                      const int32_t *shifts,
                                      uint32_t output_channels,
                                      const int8_t *output_base,
                                      uint32_t output_count,
                                      int32_t output_offset,
                                      int32_t activation_min,
                                      int32_t activation_max)
{
    metadata_multipliers = multipliers;
    metadata_shifts = shifts;
    metadata_output_channels = output_channels;
    metadata_output_base = output_base;
    metadata_output_count = output_count;
    metadata_output_offset = output_offset;
    metadata_activation_min = activation_min;
    metadata_activation_max = activation_max;
}

void qopcert_cmsis_fused_check(int32_t accumulator,
                               const int8_t *out_ptr,
                               int8_t observed,
                               int32_t multiplier,
                               int32_t shift,
                               int32_t output_offset,
                               int32_t activation_min,
                               int32_t activation_max)
{
    const uint32_t index = fused_checks++;
    if (metadata_multipliers == 0 || metadata_shifts == 0 || metadata_output_channels == 0) {
        fused_syndrome |= 1u << (index & 31u);
        return;
    }
    /* Output address integrity: the write-back cursor must stay within the
       registered output buffer. This catches cursor corruption that redirects
       strb.w to a legal-but-wrong SRAM region (input/scratch), which a
       self-consistent read-back cannot detect. */
    if (out_ptr < metadata_output_base || out_ptr >= metadata_output_base + metadata_output_count) {
        fused_syndrome |= 1u << (index & 31u);
    }
    /* This CMSIS-NN kernel emits two spatial outputs per channel before
       advancing the per-channel quantization arrays, so the write order is
       deterministic: out_0[0], out_1[0], out_0[1], out_1[1], ... with
       out_1 = out_0 + output_channels and 2*output_channels elements written
       per kernel call (output_channels is even for all Q-OpCert profiles). */
    const uint32_t pair = index >> 1;   /* which (out_0, out_1) pair   */
    const uint32_t parity = index & 1u; /* 0 = out_0 column, 1 = out_1 */
    const uint32_t channel = pair % metadata_output_channels;
    const uint32_t call = pair / metadata_output_channels;
    /* Cursor-to-cursor confusion: skipping a cursor reload leaves the previous
       column's cursor (e.g. out_1 reloaded with out_0). Both are in-range, so
       the coarse range test above cannot catch it. Reconstruct the exact
       element this check guards and require an exact match. The kernel is
       invoked once per pair of adjacent output columns and writes them to
       contiguous spatial positions: arm_convolve_s8 accumulates lhs_rows
       globally across rows, so kernel calls cover one continuous run of
       spatial columns and the single leftover column (emitted by an unchecked
       scalar MAC) lands only at the very end, not interleaved per row. */
    const int8_t *expected_out = metadata_output_base
        + (int32_t)((call * 2u + parity) * metadata_output_channels)
        + (int32_t)channel;
    if (out_ptr != expected_out) {
        fused_syndrome |= 1u << (index & 31u);
    }
    /* The checker deliberately uses metadata registered outside the optimized
       kernel. Parameters passed through the kernel remain part of the main
       path and are ignored here to avoid a common-mode metadata fault. */
    (void)multiplier;
    (void)shift;
    (void)output_offset;
    (void)activation_min;
    (void)activation_max;
#if QOPCERT_CMSIS_FUSED_MODE == 1 || QOPCERT_CMSIS_FUSED_MODE == 3 || QOPCERT_CMSIS_FUSED_MODE == 4
    int32_t expected = qopcert_shadow_requantize(
        accumulator, metadata_multipliers[channel], metadata_shifts[channel]);
#elif QOPCERT_CMSIS_FUSED_MODE == 2
    int32_t expected = qopcert_exact_requantize(
        accumulator, metadata_multipliers[channel], metadata_shifts[channel]);
#else
#error Unsupported QOPCERT_CMSIS_FUSED_MODE
#endif
    expected = clamp_i32(expected + metadata_output_offset,
                         metadata_activation_min, metadata_activation_max);
#if QOPCERT_CMSIS_FUSED_MODE == 1 || QOPCERT_CMSIS_FUSED_MODE == 3 || QOPCERT_CMSIS_FUSED_MODE == 4
    int32_t difference = expected - (int32_t)observed;
    if (difference < 0) difference = -difference;
    if (difference > 1) {
        fused_syndrome |= 1u << (index & 31u);
    }
#else
    if ((int8_t)expected != observed) {
        fused_syndrome |= 1u << (index & 31u);
    }
#endif
}

void qopcert_cmsis_return_pointer_check(const int8_t *returned, const int8_t *expected)
{
    if (returned != expected) {
        fused_return_fault |= 1u;
    }
}

uint32_t qopcert_cmsis_fused_syndrome(void)
{
    return fused_syndrome | fused_return_fault;
}

uint32_t qopcert_cmsis_fused_check_count(void)
{
    return fused_checks;
}

void qopcert_cmsis_abft_configure(const int16_t *weight_checksum,
                                  int32_t bias_checksum,
                                  int32_t columns)
{
    abft_weights = weight_checksum;
    abft_bias = bias_checksum;
    abft_columns = columns;
}

void qopcert_cmsis_abft_check(const int16_t *input_columns,
                              int32_t aligned_columns,
                              int32_t main_sum_0,
                              int32_t main_sum_1)
{
    int32_t check_0 = abft_bias;
    int32_t check_1 = abft_bias;
    const int16_t *column_0 = input_columns;
    const int16_t *column_1 = input_columns + aligned_columns;
    for (int32_t i = 0; i < abft_columns; ++i) {
        const int32_t weight = abft_weights[i];
        check_0 += weight * column_0[i];
        check_1 += weight * column_1[i];
    }
#if QOPCERT_CMSIS_FUSED_MODE == 4
    qopcert_cmsis_abft_latch_primary(check_0, main_sum_0, 1u);
    qopcert_cmsis_abft_latch_secondary(check_0, main_sum_0, 1u);
    qopcert_cmsis_abft_latch_primary(check_1, main_sum_1, 2u);
    qopcert_cmsis_abft_latch_secondary(check_1, main_sum_1, 2u);
#else
    if (check_0 != main_sum_0) abft_syndrome |= 1u;
    if (check_1 != main_sum_1) abft_syndrome |= 2u;
#endif
    abft_checks += 2;
}

#if QOPCERT_CMSIS_FUSED_MODE == 4
__attribute__((noinline))
void qopcert_cmsis_abft_latch_primary(int32_t check, int32_t main, uint32_t bit)
{
    if (check != main) abft_syndrome |= bit;
}

__attribute__((noinline))
void qopcert_cmsis_abft_latch_secondary(int32_t check, int32_t main, uint32_t bit)
{
    if (check != main) abft_syndrome_inv &= ~bit;
}
#endif

uint32_t qopcert_cmsis_abft_syndrome(void)
{
#if QOPCERT_CMSIS_FUSED_MODE == 4
    const uint32_t primary = abft_syndrome;
    const uint32_t secondary = ~abft_syndrome_inv;
    const uint32_t rail_mismatch = primary ^ secondary;
    return primary | secondary | rail_mismatch;
#else
    return abft_syndrome;
#endif
}

uint32_t qopcert_cmsis_abft_check_count(void)
{
    return abft_checks;
}

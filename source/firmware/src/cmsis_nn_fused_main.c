#include "platform.h"
#include "qopcert_api.h"
#include "qopcert_cmsis_fused.h"
#include "arm_nnfunctions.h"
#include <stdint.h>

#ifndef QOPCERT_FIRMWARE_ID
#define QOPCERT_FIRMWARE_ID "cmsis_fused_unknown"
#endif
#ifndef QOPCERT_FUSED_HARNESS_MODE
#error QOPCERT_FUSED_HARNESS_MODE must be 30..34
#endif
#ifndef QOPCERT_TRIALS
#define QOPCERT_TRIALS 128
#endif

#define FUSED_BASELINE 30
#define FUSED_SHADOW 31
#define FUSED_EXACT 32
#define FUSED_SHADOW_ABFT 33
#define FUSED_SHADOW_ABFT_HARDENED 34

/* Defaults preserve the original P0 two-column DSP-kernel experiment. */
#ifndef QOPCERT_INPUT_H
#define QOPCERT_INPUT_H 4
#endif
#ifndef QOPCERT_INPUT_W
#define QOPCERT_INPUT_W 5
#endif
#ifndef QOPCERT_INPUT_C
#define QOPCERT_INPUT_C 8
#endif
#ifndef QOPCERT_KERNEL_H
#define QOPCERT_KERNEL_H 4
#endif
#ifndef QOPCERT_KERNEL_W
#define QOPCERT_KERNEL_W 4
#endif
#ifndef QOPCERT_OUTPUT_C
#define QOPCERT_OUTPUT_C 4
#endif
#define INPUT_H QOPCERT_INPUT_H
#define INPUT_W QOPCERT_INPUT_W
#define INPUT_C QOPCERT_INPUT_C
#define KERNEL_H QOPCERT_KERNEL_H
#define KERNEL_W QOPCERT_KERNEL_W
#define OUTPUT_H (INPUT_H - KERNEL_H + 1)
#define OUTPUT_W (INPUT_W - KERNEL_W + 1)
#define OUTPUT_C QOPCERT_OUTPUT_C
#define OUTPUT_COUNT (OUTPUT_H * OUTPUT_W * OUTPUT_C)
#define RHS_COLS (KERNEL_H * KERNEL_W * INPUT_C)
#define SCRATCH_BYTES (2 * RHS_COLS * (int32_t)sizeof(int16_t))

#if (INPUT_C % 4) != 0
#error Q-OpCert profiles require INPUT_C divisible by four
#endif
#if (OUTPUT_C % 4) != 0
#error Q-OpCert profiles require OUTPUT_C divisible by four
#endif
#if (OUTPUT_COUNT % 4) != 0
#error Q-OpCert infective commit requires OUTPUT_COUNT divisible by four
#endif

extern volatile uint32_t qopcert_crash_magic;
extern volatile uint32_t qopcert_crash_count;

static int8_t input_data[INPUT_H * INPUT_W * INPUT_C];
static int8_t filter_data[OUTPUT_C * KERNEL_H * KERNEL_W * INPUT_C];
static int32_t bias_data[OUTPUT_C];
static int32_t multipliers[OUTPUT_C];
static int32_t shifts[OUTPUT_C];
static int16_t weight_checksum[RHS_COLS];
static int8_t output_data[OUTPUT_COUNT];
static int8_t golden_data[OUTPUT_COUNT];
static int32_t scratch[SCRATCH_BYTES / (int32_t)sizeof(int32_t)];
static volatile uint32_t result_sink;

static uint32_t abs_diff_i32(int32_t a, int32_t b)
{
    int32_t difference = a - b;
    return (uint32_t)(difference < 0 ? -difference : difference);
}

static int32_t clamp_i32(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void prepare_trial(uint32_t trial)
{
    for (uint32_t i = 0; i < (uint32_t)sizeof(input_data); ++i) {
        input_data[i] = (int8_t)((int32_t)((i * 17u + trial * 13u + 5u) % 31u) - 15);
    }
    for (uint32_t i = 0; i < (uint32_t)sizeof(filter_data); ++i) {
        filter_data[i] = (int8_t)((int32_t)((i * 11u + 3u) % 15u) - 7);
    }
    static const int32_t multiplier_pattern[4] = {
        1073741824, 1610612736, 1342177280, 1879048192
    };
    static const int32_t shift_pattern[4] = {-3, -4, -3, -4};
    static const int32_t bias_pattern[4] = {97, -131, 251, -313};
    for (uint32_t oc = 0; oc < OUTPUT_C; ++oc) {
        const int32_t group_delta = (int32_t)(oc >> 2) * 17;
        bias_data[oc] = bias_pattern[oc & 3u] + ((oc & 1u) ? -group_delta : group_delta);
        multipliers[oc] = multiplier_pattern[oc & 3u];
        shifts[oc] = shift_pattern[oc & 3u];
    }

    for (uint32_t base = 0; base < RHS_COLS; base += 4) {
        int32_t sums[4] = {0, 0, 0, 0};
        for (uint32_t oc = 0; oc < OUTPUT_C; ++oc) {
            for (uint32_t lane = 0; lane < 4; ++lane) {
                sums[lane] += filter_data[oc * RHS_COLS + base + lane];
            }
        }
        /* Match arm_s8_to_s16_unordered_with_offset: [0, 2, 1, 3]. */
        weight_checksum[base + 0] = (int16_t)sums[0];
        weight_checksum[base + 1] = (int16_t)sums[2];
        weight_checksum[base + 2] = (int16_t)sums[1];
        weight_checksum[base + 3] = (int16_t)sums[3];
    }

    /* Independent oracle for functional validation only; deliberately outside timing. */
    for (uint32_t oy = 0; oy < OUTPUT_H; ++oy) {
        for (uint32_t ox = 0; ox < OUTPUT_W; ++ox) {
            for (uint32_t oc = 0; oc < OUTPUT_C; ++oc) {
                int32_t accumulator = bias_data[oc];
                for (uint32_t ky = 0; ky < KERNEL_H; ++ky) {
                    for (uint32_t kx = 0; kx < KERNEL_W; ++kx) {
                        for (uint32_t ic = 0; ic < INPUT_C; ++ic) {
                            const uint32_t input_index =
                                (((oy + ky) * INPUT_W) + ox + kx) * INPUT_C + ic;
                            const uint32_t filter_index =
                                ((oc * KERNEL_H + ky) * KERNEL_W + kx) * INPUT_C + ic;
                            accumulator += (int32_t)input_data[input_index] * filter_data[filter_index];
                        }
                    }
                }
                int32_t value = qopcert_exact_requantize(accumulator, multipliers[oc], shifts[oc]);
                const uint32_t output_index = (oy * OUTPUT_W + ox) * OUTPUT_C + oc;
                golden_data[output_index] = (int8_t)clamp_i32(value, -128, 127);
                output_data[output_index] = 0x55;
            }
        }
    }
}

static uint32_t maximum_error(void)
{
    uint32_t maximum = 0;
    for (uint32_t i = 0; i < OUTPUT_COUNT; ++i) {
        const uint32_t error = abs_diff_i32(output_data[i], golden_data[i]);
        if (error > maximum) maximum = error;
    }
    return maximum;
}

static uint32_t checksum_output(void)
{
    uint32_t value = 2166136261u;
    for (uint32_t i = 0; i < OUTPUT_COUNT; ++i) {
        value = (value ^ (uint8_t)output_data[i]) * 16777619u;
    }
    return value;
}

int main(void)
{
    const cmsis_nn_context context = { .buf = scratch, .size = SCRATCH_BYTES };
    const cmsis_nn_conv_params convolution = {
        .input_offset = 0,
        .output_offset = 0,
        .stride = { .w = 1, .h = 1 },
        .padding = { .w = 0, .h = 0 },
        .dilation = { .w = 1, .h = 1 },
        .activation = { .min = -128, .max = 127 },
    };
    const cmsis_nn_per_channel_quant_params quantization = {
        .multiplier = multipliers,
        .shift = shifts,
    };
    const cmsis_nn_dims input_dims = { .n = 1, .h = INPUT_H, .w = INPUT_W, .c = INPUT_C };
    const cmsis_nn_dims filter_dims = { .n = OUTPUT_C, .h = KERNEL_H, .w = KERNEL_W, .c = INPUT_C };
    const cmsis_nn_dims bias_dims = { .n = 1, .h = 1, .w = 1, .c = OUTPUT_C };
    const cmsis_nn_dims output_dims = { .n = 1, .h = OUTPUT_H, .w = OUTPUT_W, .c = OUTPUT_C };

    platform_uart_init();
    platform_dwt_init();
    platform_uart_puts("firmware_id,mode,trial,cycles,status,syndrome,checks,abft_syndrome,abft_checks,accepted,max_abs_error,checksum,crash_magic,crash_count\n");

    for (uint32_t trial = 0; trial < QOPCERT_TRIALS; ++trial) {
        prepare_trial(trial);
        uint32_t syndrome = 0;
        uint32_t checks = 0;
        uint32_t abft_syndrome = 0;
        uint32_t abft_checks = 0;
        uint32_t accepted = 1;

        const uint32_t begin = platform_cycles_begin();
#if QOPCERT_FUSED_HARNESS_MODE != FUSED_BASELINE
        int32_t bias_checksum = 0;
        for (uint32_t oc = 0; oc < OUTPUT_C; ++oc) bias_checksum += bias_data[oc];
        qopcert_cmsis_metadata_configure(multipliers, shifts, OUTPUT_C,
                                         output_data, OUTPUT_COUNT,
                                         convolution.output_offset,
                                         convolution.activation.min,
                                         convolution.activation.max);
        qopcert_cmsis_abft_configure(weight_checksum, bias_checksum, RHS_COLS);
        qopcert_cmsis_fused_reset();
#endif
        const arm_cmsis_nn_status status = arm_convolve_s8(
            &context, &convolution, &quantization, &input_dims, input_data,
            &filter_dims, filter_data, &bias_dims, bias_data, 0,
            &output_dims, output_data);
#if QOPCERT_FUSED_HARNESS_MODE != FUSED_BASELINE
        syndrome = qopcert_cmsis_fused_syndrome();
        checks = qopcert_cmsis_fused_check_count();
        abft_syndrome = qopcert_cmsis_abft_syndrome();
        abft_checks = qopcert_cmsis_abft_check_count();
        syndrome |= abft_syndrome << 16;
        for (uint32_t base = 0; base < OUTPUT_COUNT; base += 4) {
            const uint32_t expected_epoch = 0xC0DE0000u ^ trial ^ (base * 0x9E3779B9u);
            const uint32_t observed_epoch =
                qopcert_infective_commit4(output_data + base, syndrome, expected_epoch);
            accepted &= observed_epoch == expected_epoch;
        }
#endif
        const uint32_t cycles = platform_cycles_end(begin);
        const uint32_t error = maximum_error();
        const uint32_t checksum = checksum_output();
        result_sink ^= cycles ^ (uint32_t)status ^ syndrome ^ checks ^ abft_checks ^ accepted ^ error ^ checksum;

        platform_uart_puts(QOPCERT_FIRMWARE_ID); platform_uart_putc(',');
        platform_uart_u32(QOPCERT_FUSED_HARNESS_MODE); platform_uart_putc(',');
        platform_uart_u32(trial); platform_uart_putc(',');
        platform_uart_u32(cycles); platform_uart_putc(',');
        platform_uart_u32((uint32_t)status); platform_uart_putc(',');
        platform_uart_u32(syndrome); platform_uart_putc(',');
        platform_uart_u32(checks); platform_uart_putc(',');
        platform_uart_u32(abft_syndrome); platform_uart_putc(',');
        platform_uart_u32(abft_checks); platform_uart_putc(',');
        platform_uart_u32(accepted); platform_uart_putc(',');
        platform_uart_u32(error); platform_uart_putc(',');
        platform_uart_u32(checksum); platform_uart_putc(',');
        platform_uart_u32(qopcert_crash_magic); platform_uart_putc(',');
        platform_uart_u32(qopcert_crash_count); platform_uart_putc('\n');
    }

    platform_uart_puts("END,");
    platform_uart_u32(result_sink);
    platform_uart_putc('\n');
    for (;;) { __asm__ volatile("wfi"); }
}

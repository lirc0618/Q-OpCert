#include "platform.h"
#include "qopcert_api.h"
#include "arm_nnfunctions.h"
#include <stdint.h>

#ifndef QOPCERT_FIRMWARE_ID
#define QOPCERT_FIRMWARE_ID "cmsis_layer_unknown"
#endif
#ifndef QOPCERT_LAYER_MODE
#error QOPCERT_LAYER_MODE must be 20 (baseline), 21 (shadow), or 22 (full)
#endif
#ifndef QOPCERT_TRIALS
#define QOPCERT_TRIALS 128
#endif

#define LAYER_BASELINE 20
#define LAYER_SHADOW 21
#define LAYER_FULL 22

/* A real CMSIS-NN s8 convolution layer: 4x4x8 input, 4x4 kernel, four outputs. */
#define INPUT_H 4
#define INPUT_W 4
#define INPUT_C 8
#define OUTPUT_C 4
#define KERNEL_H 4
#define KERNEL_W 4
#define RHS_COLS (KERNEL_H * KERNEL_W * INPUT_C)
#define SCRATCH_BYTES (2 * RHS_COLS * (int32_t)sizeof(int16_t))

extern volatile uint32_t qopcert_crash_magic;
extern volatile uint32_t qopcert_crash_count;

static int8_t input_data[INPUT_H * INPUT_W * INPUT_C];
static int8_t filter_data[OUTPUT_C * KERNEL_H * KERNEL_W * INPUT_C];
static int32_t bias_data[OUTPUT_C];
static int32_t multipliers[OUTPUT_C];
static int32_t shifts[OUTPUT_C];
static int8_t output_data[OUTPUT_C];
static int8_t golden_data[OUTPUT_C];
static int32_t accumulators[OUTPUT_C];
static int32_t scratch[SCRATCH_BYTES / (int32_t)sizeof(int32_t)];
static volatile uint32_t result_sink;

static uint32_t abs_diff_i32(int32_t a, int32_t b)
{
    int32_t d = a - b;
    return (uint32_t)(d < 0 ? -d : d);
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
    bias_data[0] = 97;
    bias_data[1] = -131;
    bias_data[2] = 251;
    bias_data[3] = -313;
    multipliers[0] = 1073741824;
    multipliers[1] = 1610612736;
    multipliers[2] = 1342177280;
    multipliers[3] = 1879048192;
    shifts[0] = -3;
    shifts[1] = -4;
    shifts[2] = -3;
    shifts[3] = -4;

    for (uint32_t oc = 0; oc < OUTPUT_C; ++oc) {
        int32_t acc = bias_data[oc];
        const int8_t *filter = &filter_data[oc * RHS_COLS];
        for (uint32_t i = 0; i < RHS_COLS; ++i) {
            acc += (int32_t)input_data[i] * (int32_t)filter[i];
        }
        accumulators[oc] = acc;
        int32_t value = qopcert_exact_requantize(acc, multipliers[oc], shifts[oc]);
        golden_data[oc] = (int8_t)clamp_i32(value, -128, 127);
        output_data[oc] = 0x55;
    }
}

static uint32_t maximum_error(void)
{
    uint32_t maximum = 0;
    for (uint32_t i = 0; i < OUTPUT_C; ++i) {
        const uint32_t error = abs_diff_i32(output_data[i], golden_data[i]);
        if (error > maximum) maximum = error;
    }
    return maximum;
}

static uint32_t checksum_output(void)
{
    uint32_t value = 2166136261u;
    for (uint32_t i = 0; i < OUTPUT_C; ++i) {
        value = (value ^ (uint8_t)output_data[i]) * 16777619u;
    }
    return value;
}

int main(void)
{
    const cmsis_nn_context ctx = { .buf = scratch, .size = SCRATCH_BYTES };
    const cmsis_nn_conv_params conv_params = {
        .input_offset = 0,
        .output_offset = 0,
        .stride = { .w = 1, .h = 1 },
        .padding = { .w = 0, .h = 0 },
        .dilation = { .w = 1, .h = 1 },
        .activation = { .min = -128, .max = 127 },
    };
    const cmsis_nn_per_channel_quant_params quant_params = {
        .multiplier = multipliers,
        .shift = shifts,
    };
    const cmsis_nn_dims input_dims = { .n = 1, .h = INPUT_H, .w = INPUT_W, .c = INPUT_C };
    const cmsis_nn_dims filter_dims = { .n = OUTPUT_C, .h = KERNEL_H, .w = KERNEL_W, .c = INPUT_C };
    const cmsis_nn_dims bias_dims = { .n = 1, .h = 1, .w = 1, .c = OUTPUT_C };
    const cmsis_nn_dims output_dims = { .n = 1, .h = 1, .w = 1, .c = OUTPUT_C };

    platform_uart_init();
    platform_dwt_init();
    platform_uart_puts("firmware_id,mode,trial,cycles,status,syndrome,accepted,max_abs_error,checksum,crash_magic,crash_count\n");

    for (uint32_t trial = 0; trial < QOPCERT_TRIALS; ++trial) {
        prepare_trial(trial);
        uint32_t syndrome = 0;
        uint32_t accepted = 1;
        uint32_t epoch = 0xC0DE0000u ^ trial;

        const uint32_t begin = platform_cycles_begin();
        const arm_cmsis_nn_status status = arm_convolve_s8(
            &ctx, &conv_params, &quant_params, &input_dims, input_data,
            &filter_dims, filter_data, &bias_dims, bias_data, 0,
            &output_dims, output_data);
#if QOPCERT_LAYER_MODE == LAYER_SHADOW
        syndrome = qopcert_verify4_shadow(
            accumulators, output_data, multipliers, shifts, 0, -128, 127);
        epoch = qopcert_infective_commit4(output_data, syndrome, epoch);
        accepted = (epoch == (0xC0DE0000u ^ trial));
#elif QOPCERT_LAYER_MODE == LAYER_FULL
        syndrome = qopcert_verify4_exact(
            accumulators, output_data, multipliers, shifts, 0, -128, 127);
        epoch = qopcert_infective_commit4(output_data, syndrome, epoch);
        accepted = (epoch == (0xC0DE0000u ^ trial));
#elif QOPCERT_LAYER_MODE != LAYER_BASELINE
#error Unsupported QOPCERT_LAYER_MODE
#endif
        const uint32_t cycles = platform_cycles_end(begin);
        const uint32_t error = maximum_error();
        const uint32_t checksum = checksum_output();
        result_sink ^= cycles ^ (uint32_t)status ^ syndrome ^ accepted ^ error ^ checksum;

        platform_uart_puts(QOPCERT_FIRMWARE_ID); platform_uart_putc(',');
        platform_uart_u32(QOPCERT_LAYER_MODE); platform_uart_putc(',');
        platform_uart_u32(trial); platform_uart_putc(',');
        platform_uart_u32(cycles); platform_uart_putc(',');
        platform_uart_u32((uint32_t)status); platform_uart_putc(',');
        platform_uart_u32(syndrome); platform_uart_putc(',');
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

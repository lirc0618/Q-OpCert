#include "platform.h"
#include "qopcert_api.h"
#include <stdint.h>

#ifndef QOPCERT_FIRMWARE_ID
#define QOPCERT_FIRMWARE_ID "unknown"
#endif
#ifndef QOPCERT_MODE
#define QOPCERT_MODE 1
#endif
#ifndef QOPCERT_TRIALS
#define QOPCERT_TRIALS 128
#endif
#ifndef QOPCERT_MUTANT_ID
#define QOPCERT_MUTANT_ID "NONE"
#endif

extern volatile uint32_t qopcert_crash_magic;
extern volatile uint32_t qopcert_crash_count;

static const int32_t acc[4] = { 13421773, -17179869, 33554431, -4194303 };
static const int32_t multipliers[4] = { 1073741824, 805306368, 536870912, 939524096 };
static const int32_t shifts[4] = { -3, -2, -1, -4 };
static int8_t output[4];
static volatile uint32_t result_sink;

static void csv_field(const char *s) { platform_uart_puts(s); platform_uart_putc(','); }
static void csv_u32(uint32_t v) { platform_uart_u32(v); platform_uart_putc(','); }
static void csv_i32(int32_t v) { platform_uart_i32(v); platform_uart_putc(','); }

static uint32_t max_abs_error4(const int8_t *a, const int8_t *b)
{
    uint32_t max_error = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        int32_t d = (int32_t)a[i] - (int32_t)b[i];
        if (d < 0) d = -d;
        if ((uint32_t)d > max_error) max_error = (uint32_t)d;
    }
    return max_error;
}

int main(void)
{
    platform_uart_init();
    platform_dwt_init();

    int8_t golden[4];
    qopcert_epilogue4_exact(acc, multipliers, shifts, 0, -128, 127, golden);

    platform_uart_puts("firmware_id,mutant_id,mode,trial,cycles,syndrome,accepted,max_abs_error,crash_magic,crash_count\n");

    for (uint32_t trial = 0; trial < QOPCERT_TRIALS; ++trial) {
        output[0] = output[1] = output[2] = output[3] = 0x55;
        uint32_t syndrome = 0;
        uint32_t epoch = 0xA5A50000u ^ trial;

        const uint32_t begin = platform_cycles_begin();
#if QOPCERT_MODE == 0
        qopcert_epilogue4_exact(acc, multipliers, shifts, 0, -128, 127, output);
#elif QOPCERT_MODE == 1
        syndrome = qopcert_epilogue4_fused_shadow(acc, multipliers, shifts, 0, -128, 127, output);
        epoch = qopcert_infective_commit4(output, syndrome, epoch);
#elif QOPCERT_MODE == 2
        qopcert_epilogue4_exact(acc, multipliers, shifts, 0, -128, 127, output);
        syndrome = qopcert_verify4_exact(acc, output, multipliers, shifts, 0, -128, 127);
        epoch = qopcert_infective_commit4(output, syndrome, epoch);
#else
#error Unsupported QOPCERT_MODE
#endif
        const uint32_t cycles = platform_cycles_end(begin);
        const uint32_t accepted = (epoch == (0xA5A50000u ^ trial));
        const uint32_t max_error = max_abs_error4(output, golden);
        result_sink ^= cycles ^ syndrome ^ epoch ^ max_error ^ (uint32_t)(uint8_t)output[0];

        csv_field(QOPCERT_FIRMWARE_ID);
        csv_field(QOPCERT_MUTANT_ID);
        csv_u32(QOPCERT_MODE);
        csv_u32(trial);
        csv_u32(cycles);
        csv_u32(syndrome);
        csv_u32(accepted);
        csv_u32(max_error);
        csv_u32(qopcert_crash_magic);
        platform_uart_u32(qopcert_crash_count);
        platform_uart_putc('\n');
    }

    platform_uart_puts("END,");
    platform_uart_u32(result_sink);
    platform_uart_putc('\n');
    for (;;) { __asm__ volatile("wfi"); }
}

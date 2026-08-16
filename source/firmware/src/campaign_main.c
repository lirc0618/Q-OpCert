#include "platform.h"
#include "qopcert_api.h"
#include <stdint.h>

#ifndef QOPCERT_FIRMWARE_ID
#define QOPCERT_FIRMWARE_ID "unknown"
#endif
#ifndef QOPCERT_MUTANT_ID
#define QOPCERT_MUTANT_ID "NONE"
#endif
#ifndef QOPCERT_TRIALS
#define QOPCERT_TRIALS 128
#endif
#ifndef QOPCERT_HARNESS
#error QOPCERT_HARNESS must select epilogue (10), verifier (11), or microkernel (12)
#endif

#define HARNESS_EPILOGUE 10
#define HARNESS_VERIFIER 11
#define HARNESS_MICROKERNEL 12

extern volatile uint32_t qopcert_crash_magic;
extern volatile uint32_t qopcert_crash_count;

static volatile uint32_t result_sink;

static void csv_field(const char *s) { platform_uart_puts(s); platform_uart_putc(','); }
static void csv_u32(uint32_t v) { platform_uart_u32(v); platform_uart_putc(','); }

static uint32_t abs_diff_i32(int32_t a, int32_t b)
{
    int64_t d = (int64_t)a - (int64_t)b;
    if (d < 0) d = -d;
    return (uint32_t)d;
}

static uint32_t max_abs_error4_i8(const int8_t *a, const int8_t *b)
{
    uint32_t max_error = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t d = abs_diff_i32((int32_t)a[i], (int32_t)b[i]);
        if (d > max_error) max_error = d;
    }
    return max_error;
}

static void copy4_i8(int8_t *dst, const int8_t *src)
{
    for (uint32_t i = 0; i < 4; ++i) dst[i] = src[i];
}

typedef struct {
    int32_t val;
    int32_t multiplier;
    int32_t shift;
    int32_t exact;
    int32_t shadow;
} requant_case_t;

/* Golden values are precomputed independently from the linked Phase-1 object. */
static const requant_case_t requant_cases[] = {
    {   100, 1073741824,  0,   50,   50 },
    {  -100, 1073741824,  0,  -50,  -50 },
    {    63, 1073741824,  0,   32,   32 },
    {   -63, 1073741824,  0,  -31,  -32 },
    {    25, 1073741824,  1,   25,   26 },
    {   -25, 1073741824,  1,  -25,  -24 },
    {   101, 1073741824, -1,   26,   25 },
    {  -101, 1073741824, -1,  -25,  -25 },
    {    37, 1610612736,  0,   28,   28 },
    {   -37, 1610612736,  0,  -28,  -28 },
    {  1000,  536870912, -3,   31,   31 },
    { -1000,  536870912, -3,  -31,  -32 },
    {   120, 2147483647,  0,  120,  120 },
    {  -120, 2147483647,  0, -120, -120 },
    {    70, 1073741824,  2,  140,  140 },
    {   -70, 1073741824,  2, -140, -140 },
};

static void run_epilogue_trial(uint32_t trial,
                               uint32_t *syndrome,
                               uint32_t *accepted,
                               uint32_t *max_error)
{
    const uint32_t count = (uint32_t)(sizeof(requant_cases) / sizeof(requant_cases[0]));
    uint32_t failure = 0;
    uint32_t maximum = 0;

    for (uint32_t lane = 0; lane < 4; ++lane) {
        const requant_case_t *test = &requant_cases[(trial * 4u + lane) % count];
        const int32_t exact = qopcert_exact_requantize(test->val, test->multiplier, test->shift);
        const int32_t shadow = qopcert_shadow_requantize(test->val, test->multiplier, test->shift);
        const uint32_t exact_error = abs_diff_i32(exact, test->exact);
        const uint32_t shadow_error = abs_diff_i32(shadow, test->shadow);
        if (exact_error != 0u || shadow_error != 0u) failure = 1u;
        if (exact_error > maximum) maximum = exact_error;
        if (shadow_error > maximum) maximum = shadow_error;
    }

    /* Exercise both the transparent and infective paths of the commit target. */
    const uint32_t epoch = 0xA5A50000u ^ trial;
    const int8_t initial[4] = { 11, -22, 33, -44 };
    int8_t clean[4];
    int8_t faulty[4];
    copy4_i8(clean, initial);
    copy4_i8(faulty, initial);
    const uint32_t clean_epoch = qopcert_infective_commit4(clean, 0u, epoch);
    const uint32_t faulty_epoch = qopcert_infective_commit4(faulty, 1u, epoch);
    for (uint32_t i = 0; i < 4; ++i) {
        if (clean[i] != initial[i] || faulty[i] != 0) failure = 1u;
    }
    if (clean_epoch != epoch || faulty_epoch != ~epoch) failure = 1u;

    *syndrome = failure;
    *accepted = (failure == 0u);
    *max_error = maximum;
}

typedef struct {
    int32_t acc[4];
    int32_t multipliers[4];
    int32_t shifts[4];
    int32_t dst_offset;
    int32_t activation_min;
    int32_t activation_max;
    int8_t observed[4];
} verifier_case_t;

static const verifier_case_t verifier_cases[] = {
    {
        { 100, -100, 63, -63 },
        { 1073741824, 1073741824, 1073741824, 1073741824 },
        { 0, 0, 0, 0 }, 0, -128, 127,
        { 50, -50, 32, -31 }
    },
    {
        { 25, -25, 101, -101 },
        { 1073741824, 1073741824, 1073741824, 1073741824 },
        { 1, 1, -1, -1 }, 0, -128, 127,
        { 25, -25, 26, -25 }
    },
    {
        { 37, -37, 1000, -1000 },
        { 1610612736, 1610612736, 536870912, 536870912 },
        { 0, 0, -3, -3 }, 0, -128, 127,
        { 28, -28, 31, -31 }
    },
    {
        { 120, -120, 70, -70 },
        { 2147483647, 2147483647, 1073741824, 1073741824 },
        { 0, 0, 2, 2 }, 7, -100, 100,
        { 100, -100, 100, -100 }
    },
};

static void run_verifier_trial(uint32_t trial,
                               uint32_t *syndrome,
                               uint32_t *accepted,
                               uint32_t *max_error)
{
    const uint32_t count = (uint32_t)(sizeof(verifier_cases) / sizeof(verifier_cases[0]));
    const verifier_case_t *test = &verifier_cases[trial % count];
    int8_t faulty[4];
    copy4_i8(faulty, test->observed);
    const uint32_t lane = (trial / count) & 3u;
    faulty[lane] = (int8_t)(faulty[lane] <= 124 ? faulty[lane] + 3 : faulty[lane] - 3);

    const uint32_t clean_syndrome = qopcert_verify4_shadow(
        test->acc, test->observed, test->multipliers, test->shifts,
        test->dst_offset, test->activation_min, test->activation_max);
    const uint32_t faulty_syndrome = qopcert_verify4_shadow(
        test->acc, faulty, test->multipliers, test->shifts,
        test->dst_offset, test->activation_min, test->activation_max);

    const uint32_t failure = (clean_syndrome != 0u || faulty_syndrome == 0u);
    *syndrome = failure;
    *accepted = (failure == 0u);
    *max_error = max_abs_error4_i8(faulty, test->observed);
}

typedef struct {
    int8_t lhs0[12];
    int8_t lhs1[12];
    int8_t rhs0[12];
    int8_t rhs1[12];
    int32_t cols;
    int32_t bias[2];
    int32_t multipliers[2];
    int32_t shifts[2];
    int32_t dst_offset;
    int8_t golden[4];
} microkernel_case_t;

static const microkernel_case_t microkernel_cases[] = {
    {
        { 1, 2, 3, 4 },
        { -1, 2, -3, 4 },
        { 5, -6, 7, -8 },
        { -2, 3, -4, 5 },
        4, { 10, -10 }, { 1073741824, 1610612736 }, { 0, 0 }, 0,
        { -4, 2, -30, 23 }
    },
    {
        { 12, -7, 5, 3, -4, 9, -2, 6 },
        { -8, 11, -3, 7, 2, -5, 4, -9 },
        { 3, -6, 8, -2, 5, -7, 1, 4 },
        { -5, 2, -4, 9, -1, 6, -8, 3 },
        8, { 17, -23 }, { 1610612736, 1073741824 }, { -1, 1 }, -3,
        { 23, -1, -40, 20 }
    },
    {
        { 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50 },
        { -50, -50, -50, -50, -50, -50, -50, -50, -50, -50, -50, -50 },
        { 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50 },
        { -50, -50, -50, -50, -50, -50, -50, -50, -50, -50, -50, -50 },
        12, { 0, 0 }, { 2147483647, 2147483647 }, { 0, 0 }, 0,
        { 127, -128, -128, 127 }
    },
};

static void microkernel_accumulators(const microkernel_case_t *test, int32_t *acc)
{
    acc[0] = test->bias[0];
    acc[1] = test->bias[1];
    acc[2] = test->bias[0];
    acc[3] = test->bias[1];
    for (int32_t i = 0; i < test->cols; ++i) {
        acc[0] += (int32_t)test->lhs0[i] * (int32_t)test->rhs0[i];
        acc[1] += (int32_t)test->lhs0[i] * (int32_t)test->rhs1[i];
        acc[2] += (int32_t)test->lhs1[i] * (int32_t)test->rhs0[i];
        acc[3] += (int32_t)test->lhs1[i] * (int32_t)test->rhs1[i];
    }
}

static void run_microkernel_trial(uint32_t trial,
                                  uint32_t *syndrome,
                                  uint32_t *accepted,
                                  uint32_t *max_error)
{
    const uint32_t count = (uint32_t)(sizeof(microkernel_cases) / sizeof(microkernel_cases[0]));
    const microkernel_case_t *test = &microkernel_cases[trial % count];
    int8_t output[4] = { 0x55, 0x55, 0x55, 0x55 };
    int8_t observed[4];
    int32_t acc[4];
    int32_t multipliers4[4] = {
        test->multipliers[0], test->multipliers[1],
        test->multipliers[0], test->multipliers[1]
    };
    int32_t shifts4[4] = {
        test->shifts[0], test->shifts[1], test->shifts[0], test->shifts[1]
    };

    microkernel_accumulators(test, acc);
    qopcert_microkernel_2x2_exact(
        test->lhs0, test->lhs1, test->rhs0, test->rhs1, test->cols,
        test->bias, test->multipliers, test->shifts, test->dst_offset, output);
    copy4_i8(observed, output);

    const uint32_t raw_syndrome = qopcert_verify4_shadow(
        acc, observed, multipliers4, shifts4, test->dst_offset, -128, 127);
    const uint32_t epoch = 0x5A5A0000u ^ trial;
    const uint32_t committed_epoch = qopcert_infective_commit4(output, raw_syndrome, epoch);

    *syndrome = raw_syndrome;
    *accepted = (committed_epoch == epoch);
    *max_error = max_abs_error4_i8(observed, test->golden);
}

int main(void)
{
    platform_uart_init();
    platform_dwt_init();
    platform_uart_puts("firmware_id,mutant_id,mode,trial,cycles,syndrome,accepted,max_abs_error,crash_magic,crash_count\n");

    for (uint32_t trial = 0; trial < QOPCERT_TRIALS; ++trial) {
        uint32_t syndrome = 0;
        uint32_t accepted = 0;
        uint32_t max_error = 0;
        const uint32_t begin = platform_cycles_begin();
#if QOPCERT_HARNESS == HARNESS_EPILOGUE
        run_epilogue_trial(trial, &syndrome, &accepted, &max_error);
#elif QOPCERT_HARNESS == HARNESS_VERIFIER
        run_verifier_trial(trial, &syndrome, &accepted, &max_error);
#elif QOPCERT_HARNESS == HARNESS_MICROKERNEL
        run_microkernel_trial(trial, &syndrome, &accepted, &max_error);
#else
#error Unsupported QOPCERT_HARNESS
#endif
        const uint32_t cycles = platform_cycles_end(begin);
        result_sink ^= cycles ^ syndrome ^ accepted ^ max_error;

        csv_field(QOPCERT_FIRMWARE_ID);
        csv_field(QOPCERT_MUTANT_ID);
        csv_u32(QOPCERT_HARNESS);
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

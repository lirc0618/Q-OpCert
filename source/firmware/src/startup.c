#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
int main(void);

volatile uint32_t qopcert_crash_magic __attribute__((section(".noinit")));
volatile uint32_t qopcert_crash_count __attribute__((section(".noinit")));

void Reset_Handler(void);
void Default_Handler(void);
void HardFault_Handler(void);

__attribute__((section(".isr_vector"), used))
const uintptr_t vector_table[] = {
    (uintptr_t)&_estack,
    (uintptr_t)Reset_Handler,
    (uintptr_t)Default_Handler,
    (uintptr_t)HardFault_Handler,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    0, 0, 0, 0,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
    0,
    (uintptr_t)Default_Handler,
    (uintptr_t)Default_Handler,
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ++dst) {
        *dst = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss; ++dst) {
        *dst = 0;
    }
    /* `.noinit` is retained by the linker, so clear stale power-up SRAM state. */
    qopcert_crash_magic = 0;
    qopcert_crash_count = 0;
    (void)main();
    for (;;) { __asm__ volatile("wfi"); }
}

void HardFault_Handler(void)
{
    qopcert_crash_magic = 0x514F5046u; /* QOPF */
    qopcert_crash_count++;
    for (;;) { __asm__ volatile("bkpt #0"); }
}

void Default_Handler(void)
{
    for (;;) { __asm__ volatile("wfi"); }
}

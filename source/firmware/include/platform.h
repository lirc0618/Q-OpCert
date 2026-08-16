#ifndef QOPCERT_PLATFORM_H
#define QOPCERT_PLATFORM_H
#include <stdint.h>
void platform_uart_init(void);
void platform_uart_putc(char c);
void platform_uart_puts(const char *s);
void platform_uart_u32(uint32_t value);
void platform_uart_i32(int32_t value);
void platform_dwt_init(void);
uint32_t platform_cycles_begin(void);
uint32_t platform_cycles_end(uint32_t begin);
#endif

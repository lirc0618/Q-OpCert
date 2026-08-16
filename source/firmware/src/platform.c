#include "stm32f407_min.h"
#include <stdint.h>

void platform_uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOA;
    RCC_APB2ENR |= RCC_APB2ENR_USART1;
    (void)RCC_AHB1ENR;
    (void)RCC_APB2ENR;

    /* PA9 = USART1_TX, alternate function 7. HSI remains at 16 MHz. */
    GPIOA_MODER = (GPIOA_MODER & ~(3u << (9u * 2u))) | (2u << (9u * 2u));
    GPIOA_OSPEEDR |= (3u << (9u * 2u));
    GPIOA_AFRH = (GPIOA_AFRH & ~(0xFu << 4u)) | (7u << 4u);

    USART1_CR1 = 0;
    USART1_BRR = 0x008Bu; /* 16 MHz / 115200 baud, oversampling by 16. */
    USART1_CR1 = USART_CR1_TE | USART_CR1_UE;
}

void platform_uart_putc(char c)
{
    while ((USART1_SR & USART_SR_TXE) == 0u) { }
    USART1_DR = (uint32_t)(uint8_t)c;
}

void platform_uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') platform_uart_putc('\r');
        platform_uart_putc(*s++);
    }
}

void platform_uart_u32(uint32_t value)
{
    char buf[10];
    uint32_t n = 0;
    do {
        buf[n++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    while (n != 0u) platform_uart_putc(buf[--n]);
}

void platform_uart_i32(int32_t value)
{
    if (value < 0) {
        platform_uart_putc('-');
        platform_uart_u32((uint32_t)(-(int64_t)value));
    } else {
        platform_uart_u32((uint32_t)value);
    }
}

void platform_dwt_init(void)
{
    COREDEBUG_DEMCR |= DEMCR_TRCENA;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CYCCNTENA;
    data_sync_barrier();
    instr_sync_barrier();
}

uint32_t platform_cycles_begin(void)
{
    data_sync_barrier();
    instr_sync_barrier();
    return DWT_CYCCNT;
}

uint32_t platform_cycles_end(uint32_t begin)
{
    data_sync_barrier();
    instr_sync_barrier();
    return DWT_CYCCNT - begin;
}

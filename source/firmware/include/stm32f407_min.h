#ifndef STM32F407_MIN_H
#define STM32F407_MIN_H

#include <stdint.h>

#define MMIO32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

#define RCC_BASE          0x40023800u
#define RCC_AHB1ENR       MMIO32(RCC_BASE + 0x30u)
#define RCC_APB2ENR       MMIO32(RCC_BASE + 0x44u)
#define RCC_AHB1ENR_GPIOA (1u << 0)
#define RCC_APB2ENR_USART1 (1u << 4)

#define GPIOA_BASE        0x40020000u
#define GPIOA_MODER       MMIO32(GPIOA_BASE + 0x00u)
#define GPIOA_OSPEEDR     MMIO32(GPIOA_BASE + 0x08u)
#define GPIOA_AFRH        MMIO32(GPIOA_BASE + 0x24u)

#define USART1_BASE       0x40011000u
#define USART1_SR         MMIO32(USART1_BASE + 0x00u)
#define USART1_DR         MMIO32(USART1_BASE + 0x04u)
#define USART1_BRR        MMIO32(USART1_BASE + 0x08u)
#define USART1_CR1        MMIO32(USART1_BASE + 0x0Cu)
#define USART_SR_TXE      (1u << 7)
#define USART_CR1_TE      (1u << 3)
#define USART_CR1_UE      (1u << 13)

#define COREDEBUG_DEMCR   MMIO32(0xE000EDFCu)
#define DWT_CTRL          MMIO32(0xE0001000u)
#define DWT_CYCCNT        MMIO32(0xE0001004u)
#define DEMCR_TRCENA      (1u << 24)
#define DWT_CYCCNTENA     (1u << 0)

static inline void data_sync_barrier(void)
{
    __asm__ volatile("dsb 0xF" ::: "memory");
}

static inline void instr_sync_barrier(void)
{
    __asm__ volatile("isb 0xF" ::: "memory");
}

#endif

#include "platform.h"
#include "stm32u5xx_hal.h"

static UART_HandleTypeDef uart;
volatile uint32_t qopcert_crash_magic;
volatile uint32_t qopcert_crash_count;

static void fail(void) { __disable_irq(); while (1) { } }

static void clock_config(void)
{
  RCC_ClkInitTypeDef clk = {0};
  RCC_OscInitTypeDef osc = {0};
  __HAL_RCC_PWR_CLK_ENABLE();
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) fail();
  if (HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK) fail();
  __HAL_RCC_PWR_CLK_DISABLE();
  osc.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  osc.MSIState = RCC_MSI_ON;
  osc.MSIClockRange = RCC_MSIRANGE_4;
  osc.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  osc.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
  osc.PLL.PLLM = 1; osc.PLL.PLLN = 80;
  osc.PLL.PLLR = 2; osc.PLL.PLLP = 2; osc.PLL.PLLQ = 2;
  osc.PLL.PLLFRACN = 0; osc.PLL.PLLRGE = RCC_PLLVCIRANGE_0;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) fail();
  clk.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 |
                  RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_PCLK3;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  clk.APB3CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK) fail();
}

void platform_uart_init(void)
{
  GPIO_InitTypeDef gpio = {0};
  RCC_PeriphCLKInitTypeDef periph = {0};
  HAL_Init();
  if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_1WAY) != HAL_OK) fail();
  if (HAL_ICACHE_Enable() != HAL_OK) fail();
  clock_config();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  periph.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  periph.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) fail();
  __HAL_RCC_USART1_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_AF_PP; gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW; gpio.Alternate = GPIO_AF7_USART1;
  HAL_GPIO_Init(GPIOA, &gpio);
  uart.Instance = USART1;
  uart.Init.BaudRate = 115200; uart.Init.WordLength = UART_WORDLENGTH_8B;
  uart.Init.StopBits = UART_STOPBITS_1; uart.Init.Parity = UART_PARITY_NONE;
  uart.Init.Mode = UART_MODE_TX_RX; uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  uart.Init.OverSampling = UART_OVERSAMPLING_16;
  uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&uart) != HAL_OK) fail();
  qopcert_crash_magic = 0; qopcert_crash_count = 0;
}

void platform_uart_putc(char c)
{
  uint8_t byte = (uint8_t)c;
  if (HAL_UART_Transmit(&uart, &byte, 1, HAL_MAX_DELAY) != HAL_OK) fail();
}

void platform_uart_puts(const char *s)
{
  while (*s) { if (*s == '\n') platform_uart_putc('\r'); platform_uart_putc(*s++); }
}

void platform_uart_u32(uint32_t value)
{
  char b[10]; uint32_t n = 0;
  do { b[n++] = (char)('0' + value % 10U); value /= 10U; } while (value != 0U);
  while (n) platform_uart_putc(b[--n]);
}

void platform_uart_i32(int32_t value)
{
  if (value < 0) { platform_uart_putc('-'); platform_uart_u32((uint32_t)(-(int64_t)value)); }
  else platform_uart_u32((uint32_t)value);
}

void platform_dwt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  __DSB(); __ISB();
}

uint32_t platform_cycles_begin(void) { __DSB(); __ISB(); return DWT->CYCCNT; }
uint32_t platform_cycles_end(uint32_t begin) { __DSB(); __ISB(); return DWT->CYCCNT - begin; }

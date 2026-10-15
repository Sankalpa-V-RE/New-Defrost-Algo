#include "software_uart.h"

static GPIO_TypeDef *software_uart_port;
static uint16_t software_uart_pin;
static uint16_t software_uart_timer_ticks_per_bit;

static __attribute__((always_inline)) inline void SoftwareUart_SetLine(bool high)
{
  if (high)
  {
    software_uart_port->BSRR = software_uart_pin;
  }
  else
  {
    software_uart_port->BRR = software_uart_pin;
  }
}

static __attribute__((always_inline)) inline void
SoftwareUart_WaitForDeadline(uint16_t deadline)
{
  while ((int16_t)((uint16_t)TIM3->CNT - deadline) < 0)
  {
  }
}

/*
 * This test-only transmitter uses TIM3 as a free-running 12 MHz timebase.
 * Absolute deadlines prevent instruction overhead from accumulating across
 * the ten bits in a byte. Interrupts are disabled for one byte only.
 */
static __attribute__((optimize("O2"))) void SoftwareUart_WriteByte(uint8_t value)
{
  uint32_t saved_interrupt_mask = __get_PRIMASK();
  uint16_t deadline;
  uint8_t bit_index;

  __disable_irq();

  SoftwareUart_SetLine(false);
  deadline = (uint16_t)(TIM3->CNT + software_uart_timer_ticks_per_bit);
  SoftwareUart_WaitForDeadline(deadline);

  for (bit_index = 0U; bit_index < 8U; ++bit_index)
  {
    SoftwareUart_SetLine((value & (1U << bit_index)) != 0U);
    deadline = (uint16_t)(deadline + software_uart_timer_ticks_per_bit);
    SoftwareUart_WaitForDeadline(deadline);
  }

  SoftwareUart_SetLine(true);
  deadline = (uint16_t)(deadline + software_uart_timer_ticks_per_bit);
  SoftwareUart_WaitForDeadline(deadline);

  if (saved_interrupt_mask == 0U)
  {
    __enable_irq();
  }
}

void SoftwareUart_Initialize(GPIO_TypeDef *port, uint16_t pin, uint32_t baud_rate)
{
  uint32_t timer_clock_hertz = HAL_RCC_GetPCLK1Freq();

  software_uart_port = port;
  software_uart_pin = pin;
  software_uart_timer_ticks_per_bit =
      (uint16_t)((timer_clock_hertz + (baud_rate / 2U)) / baud_rate);

  __HAL_RCC_TIM3_CLK_ENABLE();
  TIM3->CR1 = 0U;
  TIM3->PSC = 0U;
  TIM3->ARR = 0xFFFFU;
  TIM3->CNT = 0U;
  TIM3->EGR = TIM_EGR_UG;
  TIM3->SR = 0U;
  TIM3->CR1 = TIM_CR1_CEN;

  SoftwareUart_SetLine(true);
}

void SoftwareUart_Write(const char *text)
{
  if (text == NULL)
  {
    return;
  }

  while (*text != '\0')
  {
    SoftwareUart_WriteByte((uint8_t)*text);
    ++text;
  }
}

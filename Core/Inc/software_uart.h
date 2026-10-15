#ifndef SOFTWARE_UART_H
#define SOFTWARE_UART_H

#include "stm32c0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

void SoftwareUart_Initialize(GPIO_TypeDef *port, uint16_t pin, uint32_t baud_rate);
void SoftwareUart_Write(const char *text);

#endif

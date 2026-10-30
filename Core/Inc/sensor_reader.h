#ifndef SENSOR_READER_H
#define SENSOR_READER_H

#include "stm32c0xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#define SENSOR_FRAME_LENGTH_BYTES            8U

typedef enum
{
  SENSOR_READER_STATUS_VALID = 0,
  SENSOR_READER_STATUS_NO_VALID_FRAME,
  SENSOR_READER_STATUS_UART_CONFIGURATION_ERROR
} SensorReaderStatus;

typedef struct
{
  SensorReaderStatus status;
  /** Sensor temperature in signed hundredths of a degree Celsius. */
  int16_t temperature_centi_celsius;
  uint16_t capacitance_picofarads;
  uint8_t frame[SENSOR_FRAME_LENGTH_BYTES];
  uint8_t attempts_used;
  uint16_t crc_error_count;
  uint16_t framing_error_count;
  uint16_t uart_error_count;
  uint32_t acquisition_duration_milliseconds;
} SensorReaderResult;

bool SensorReader_Acquire(UART_HandleTypeDef *sensor_uart,
                          IWDG_HandleTypeDef *watchdog,
                          uint8_t maximum_attempts,
                          SensorReaderResult *result);

uint16_t SensorReader_CalculateCrc16CcittFalse(const uint8_t *data,
                                                uint32_t length);

#endif

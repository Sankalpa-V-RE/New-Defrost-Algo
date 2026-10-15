#include "sensor_reader.h"

#include "main.h"

#include <string.h>

#define SENSOR_FRAME_START_MARKER  0xAAU
#define SENSOR_FRAME_END_MARKER    0xACU
#define UART_POLL_SLICE_MILLISECONDS  50U

static void SensorReader_ClearResult(SensorReaderResult *result)
{
  memset(result, 0, sizeof(*result));
  result->status = SENSOR_READER_STATUS_NO_VALID_FRAME;
}

static void SensorReader_ClearUartReceiveState(UART_HandleTypeDef *sensor_uart)
{
  __HAL_UART_CLEAR_FLAG(sensor_uart,
                        UART_CLEAR_PEF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_OREF);
  __HAL_UART_SEND_REQ(sensor_uart, UART_RXDATA_FLUSH_REQUEST);
}

static uint32_t SensorReader_ElapsedMilliseconds(uint32_t start_tick)
{
  return HAL_GetTick() - start_tick;
}

static uint8_t SensorReader_KeepTrailingPartialFrame(uint8_t *frame,
                                                     uint8_t frame_length)
{
  uint8_t marker_index;

  for (marker_index = 1U; marker_index < frame_length; ++marker_index)
  {
    if (frame[marker_index] == SENSOR_FRAME_START_MARKER)
    {
      uint8_t trailing_length = (uint8_t)(frame_length - marker_index);
      memmove(frame, &frame[marker_index], trailing_length);
      return trailing_length;
    }
  }

  return 0U;
}

static bool SensorReader_DecodeFrame(const uint8_t *frame,
                                     SensorReaderResult *result)
{
  uint16_t received_crc;
  uint16_t calculated_crc;
  uint16_t temperature_bits;

  if ((frame[0] != SENSOR_FRAME_START_MARKER) ||
      (frame[7] != SENSOR_FRAME_END_MARKER))
  {
    ++result->framing_error_count;
    return false;
  }

  received_crc = ((uint16_t)frame[5] << 8) | frame[6];
  calculated_crc = SensorReader_CalculateCrc16CcittFalse(&frame[1], 4U);

  if (received_crc != calculated_crc)
  {
    ++result->crc_error_count;
    return false;
  }

  temperature_bits = ((uint16_t)frame[1] << 8) | frame[2];
  result->temperature_celsius = (int16_t)temperature_bits;
  result->capacitance_picofarads = ((uint16_t)frame[3] << 8) | frame[4];
  memcpy(result->frame, frame, SENSOR_FRAME_LENGTH_BYTES);
  result->status = SENSOR_READER_STATUS_VALID;
  return true;
}

static bool SensorReader_ReadPoweredStream(UART_HandleTypeDef *sensor_uart,
                                           IWDG_HandleTypeDef *watchdog,
                                           SensorReaderResult *result)
{
  uint8_t frame[SENSOR_FRAME_LENGTH_BYTES] = {0U};
  uint8_t frame_length = 0U;
  uint32_t receive_start_tick = HAL_GetTick();

  while (SensorReader_ElapsedMilliseconds(receive_start_tick) <
         SENSOR_RECEIVE_TIMEOUT_MILLISECONDS)
  {
    uint8_t received_byte = 0U;
    uint32_t elapsed = SensorReader_ElapsedMilliseconds(receive_start_tick);
    uint32_t remaining = SENSOR_RECEIVE_TIMEOUT_MILLISECONDS - elapsed;
    uint32_t poll_timeout = remaining;
    HAL_StatusTypeDef receive_status;

    if (poll_timeout > UART_POLL_SLICE_MILLISECONDS)
    {
      poll_timeout = UART_POLL_SLICE_MILLISECONDS;
    }

    HAL_IWDG_Refresh(watchdog);
    receive_status = HAL_UART_Receive(sensor_uart, &received_byte, 1U, poll_timeout);

    if (receive_status == HAL_TIMEOUT)
    {
      continue;
    }

    if (receive_status != HAL_OK)
    {
      ++result->uart_error_count;
      SensorReader_ClearUartReceiveState(sensor_uart);
      frame_length = 0U;
      continue;
    }

    if (frame_length == 0U)
    {
      if (received_byte == SENSOR_FRAME_START_MARKER)
      {
        frame[0] = received_byte;
        frame_length = 1U;
      }
      continue;
    }

    frame[frame_length] = received_byte;
    ++frame_length;

    if (frame_length == SENSOR_FRAME_LENGTH_BYTES)
    {
      if (SensorReader_DecodeFrame(frame, result))
      {
        return true;
      }

      frame_length = SensorReader_KeepTrailingPartialFrame(frame, frame_length);
    }
  }

  return false;
}

bool SensorReader_Acquire(UART_HandleTypeDef *sensor_uart,
                          IWDG_HandleTypeDef *watchdog,
                          SensorReaderResult *result)
{
  uint32_t acquisition_start_tick;
  uint8_t attempt;

  if ((sensor_uart == NULL) || (watchdog == NULL) || (result == NULL))
  {
    return false;
  }

  SensorReader_ClearResult(result);
  acquisition_start_tick = HAL_GetTick();

  HAL_GPIO_WritePin(CAP_SENSOR_EN_GPIO_Port, CAP_SENSOR_EN_Pin, GPIO_PIN_RESET);

  if (HAL_HalfDuplex_EnableReceiver(sensor_uart) != HAL_OK)
  {
    result->status = SENSOR_READER_STATUS_UART_CONFIGURATION_ERROR;
    result->acquisition_duration_milliseconds =
        SensorReader_ElapsedMilliseconds(acquisition_start_tick);
    return false;
  }

  for (attempt = 1U; attempt <= SENSOR_MAXIMUM_ACQUISITION_ATTEMPTS; ++attempt)
  {
    result->attempts_used = attempt;
    SensorReader_ClearUartReceiveState(sensor_uart);
    HAL_IWDG_Refresh(watchdog);

    /* Reception is ready before power-on; the sensor starts streaming after about 50 ms. */
    HAL_GPIO_WritePin(CAP_SENSOR_EN_GPIO_Port, CAP_SENSOR_EN_Pin, GPIO_PIN_SET);

    if (SensorReader_ReadPoweredStream(sensor_uart, watchdog, result))
    {
      HAL_Delay(SENSOR_POST_RECEIVE_MILLISECONDS);
      HAL_GPIO_WritePin(CAP_SENSOR_EN_GPIO_Port, CAP_SENSOR_EN_Pin, GPIO_PIN_RESET);
      result->acquisition_duration_milliseconds =
          SensorReader_ElapsedMilliseconds(acquisition_start_tick);
      return true;
    }

    HAL_GPIO_WritePin(CAP_SENSOR_EN_GPIO_Port, CAP_SENSOR_EN_Pin, GPIO_PIN_RESET);

    if (attempt < SENSOR_MAXIMUM_ACQUISITION_ATTEMPTS)
    {
      HAL_IWDG_Refresh(watchdog);
      HAL_Delay(SENSOR_RETRY_OFF_MILLISECONDS);
    }
  }

  result->acquisition_duration_milliseconds =
      SensorReader_ElapsedMilliseconds(acquisition_start_tick);
  return false;
}

uint16_t SensorReader_CalculateCrc16CcittFalse(const uint8_t *data,
                                                uint32_t length)
{
  uint16_t crc = 0xFFFFU;
  uint32_t byte_index;

  for (byte_index = 0U; byte_index < length; ++byte_index)
  {
    uint8_t bit_index;

    crc ^= (uint16_t)data[byte_index] << 8;

    for (bit_index = 0U; bit_index < 8U; ++bit_index)
    {
      if ((crc & 0x8000U) != 0U)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

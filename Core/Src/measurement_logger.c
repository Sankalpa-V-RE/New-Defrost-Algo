#include "measurement_logger.h"

#include "main.h"
#include "software_uart.h"

#include <stddef.h>

#define LOG_BUFFER_LENGTH  192U
#define DEBUG_LOG_BAUD_RATE  115200U

static size_t MeasurementLogger_AppendCharacter(char *buffer,
                                                size_t position,
                                                char character)
{
  if (position < (LOG_BUFFER_LENGTH - 1U))
  {
    buffer[position] = character;
    ++position;
  }
  return position;
}

static size_t MeasurementLogger_AppendText(char *buffer,
                                           size_t position,
                                           const char *text)
{
  while ((*text != '\0') && (position < (LOG_BUFFER_LENGTH - 1U)))
  {
    buffer[position] = *text;
    ++position;
    ++text;
  }
  return position;
}

static size_t MeasurementLogger_AppendUnsigned(char *buffer,
                                               size_t position,
                                               uint32_t value)
{
  char digits[10];
  uint8_t digit_count = 0U;

  do
  {
    digits[digit_count] = (char)('0' + (value % 10U));
    ++digit_count;
    value /= 10U;
  } while ((value != 0U) && (digit_count < sizeof(digits)));

  while (digit_count > 0U)
  {
    --digit_count;
    position = MeasurementLogger_AppendCharacter(buffer, position, digits[digit_count]);
  }

  return position;
}

static size_t MeasurementLogger_AppendSigned(char *buffer,
                                             size_t position,
                                             int32_t value)
{
  uint32_t magnitude;

  if (value < 0)
  {
    position = MeasurementLogger_AppendCharacter(buffer, position, '-');
    magnitude = (uint32_t)(-(value + 1)) + 1U;
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  return MeasurementLogger_AppendUnsigned(buffer, position, magnitude);
}

static size_t MeasurementLogger_AppendSignedHundredths(char *buffer,
                                                       size_t position,
                                                       int16_t scaled_value)
{
  int32_t signed_value = scaled_value;
  uint32_t magnitude;
  uint32_t fractional_part;

  if (signed_value < 0)
  {
    position = MeasurementLogger_AppendCharacter(buffer, position, '-');
    magnitude = (uint32_t)(-signed_value);
  }
  else
  {
    magnitude = (uint32_t)signed_value;
  }

  position = MeasurementLogger_AppendUnsigned(buffer, position, magnitude / 100U);
  position = MeasurementLogger_AppendCharacter(buffer, position, '.');
  fractional_part = magnitude % 100U;

  if (fractional_part < 10U)
  {
    position = MeasurementLogger_AppendCharacter(buffer, position, '0');
  }

  return MeasurementLogger_AppendUnsigned(buffer, position, fractional_part);
}

static size_t MeasurementLogger_AppendHexByte(char *buffer,
                                              size_t position,
                                              uint8_t value)
{
  static const char hex_digits[] = "0123456789ABCDEF";

  position = MeasurementLogger_AppendCharacter(buffer,
                                                position,
                                                hex_digits[(value >> 4) & 0x0FU]);
  return MeasurementLogger_AppendCharacter(buffer,
                                           position,
                                           hex_digits[value & 0x0FU]);
}

static const char *MeasurementLogger_StatusText(SensorReaderStatus status)
{
  switch (status)
  {
    case SENSOR_READER_STATUS_VALID:
      return "VALID";
    case SENSOR_READER_STATUS_UART_CONFIGURATION_ERROR:
      return "UART_CONFIG";
    case SENSOR_READER_STATUS_NO_VALID_FRAME:
    default:
      return "NO_FRAME";
  }
}

void MeasurementLogger_Initialize(void)
{
  SoftwareUart_Initialize(softUART_TX_GPIO_Port,
                          softUART_TX_Pin,
                          DEBUG_LOG_BAUD_RATE);
}

void MeasurementLogger_WriteHeader(void)
{
  SoftwareUart_Write("\r\nUART_READY,115200,8N1\r\n");
  SoftwareUart_Write("ms,sequence,status,relay_energized,temperature_c,temperature_raw_x100,capacitance_raw_be,attempts,crc_errors,frame_errors,uart_errors,acquisition_ms,frame_hex\r\n");
}

void MeasurementLogger_WriteResult(uint32_t acquisition_start_milliseconds,
                                   uint32_t sequence_number,
                                   const SensorReaderResult *result)
{
  char buffer[LOG_BUFFER_LENGTH];
  size_t position = 0U;
  uint8_t byte_index;

  position = MeasurementLogger_AppendUnsigned(buffer, position, acquisition_start_milliseconds);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(buffer, position, sequence_number);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendText(buffer, position, MeasurementLogger_StatusText(result->status));
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(
      buffer,
      position,
      ((RELAY_GPIO_Port->ODR & RELAY_Pin) != 0U) ? 1U : 0U);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');

  if (result->status == SENSOR_READER_STATUS_VALID)
  {
    position = MeasurementLogger_AppendSignedHundredths(
        buffer,
        position,
        result->temperature_celsius);
  }

  position = MeasurementLogger_AppendCharacter(buffer, position, ',');

  if (result->status == SENSOR_READER_STATUS_VALID)
  {
    position = MeasurementLogger_AppendSigned(
        buffer,
        position,
        result->temperature_celsius);
  }

  position = MeasurementLogger_AppendCharacter(buffer, position, ',');

  if (result->status == SENSOR_READER_STATUS_VALID)
  {
    position = MeasurementLogger_AppendUnsigned(buffer, position, result->capacitance_picofarads);
  }

  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(buffer, position, result->attempts_used);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(buffer, position, result->crc_error_count);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(buffer, position, result->framing_error_count);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(buffer, position, result->uart_error_count);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');
  position = MeasurementLogger_AppendUnsigned(buffer, position, result->acquisition_duration_milliseconds);
  position = MeasurementLogger_AppendCharacter(buffer, position, ',');

  if (result->status == SENSOR_READER_STATUS_VALID)
  {
    for (byte_index = 0U; byte_index < SENSOR_FRAME_LENGTH_BYTES; ++byte_index)
    {
      position = MeasurementLogger_AppendHexByte(buffer, position, result->frame[byte_index]);
    }
  }

  position = MeasurementLogger_AppendCharacter(buffer, position, '\r');
  position = MeasurementLogger_AppendCharacter(buffer, position, '\n');
  buffer[position] = '\0';
  SoftwareUart_Write(buffer);
}

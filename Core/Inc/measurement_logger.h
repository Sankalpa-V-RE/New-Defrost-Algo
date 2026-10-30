#ifndef MEASUREMENT_LOGGER_H
#define MEASUREMENT_LOGGER_H

#include "sensor_reader.h"

#include <stdbool.h>
#include <stdint.h>

/** Controller snapshot associated with one sensor log record.
 *  State/event strings must be static text owned by the caller and contain no
 *  commas or line endings. Durations are milliseconds; cold exposure is
 *  centi-degree-Celsius-seconds.
 */
typedef struct
{
  const char *controller_state;
  const char *event;
  bool defrost_active;
  bool relay_energized;
  uint32_t cold_exposure_cdegc_seconds;
  uint32_t session_elapsed_milliseconds;
  uint32_t above_upper_band_milliseconds;
  uint32_t lockout_remaining_milliseconds;
  uint8_t consecutive_sensor_failures;
} MeasurementLoggerControllerContext;

void MeasurementLogger_Initialize(void);
void MeasurementLogger_WriteHeader(void);
void MeasurementLogger_WriteResult(uint32_t acquisition_start_milliseconds,
                                   uint32_t sequence_number,
                                   const MeasurementLoggerControllerContext *context,
                                   const SensorReaderResult *result);

#endif

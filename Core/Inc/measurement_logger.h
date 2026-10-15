#ifndef MEASUREMENT_LOGGER_H
#define MEASUREMENT_LOGGER_H

#include "sensor_reader.h"

#include <stdint.h>

void MeasurementLogger_Initialize(void);
void MeasurementLogger_WriteHeader(void);
void MeasurementLogger_WriteResult(uint32_t acquisition_start_milliseconds,
                                   uint32_t sequence_number,
                                   const SensorReaderResult *result);

#endif

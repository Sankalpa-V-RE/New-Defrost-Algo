/**
 * @file defrost_config.h
 * @brief Rebuild-time parameters for the defrost controller.
 *
 * This is the single firmware parameter file for the defrost algorithm and its
 * safety limits. Keep hardware pin/peripheral configuration in the CubeMX .ioc
 * file instead. All temperatures use centi-degrees Celsius, all capacitance
 * values remain raw sensor picofarads, and cold exposure uses
 * centi-degree-Celsius-seconds (cdegC*s).
 *
 * These are initial experimental values, not production-approved constants.
 * Change them only through an explicit design decision and rebuild the firmware.
 */

#ifndef DEFROST_CONFIG_H
#define DEFROST_CONFIG_H

/* -------------------------------------------------------------------------- */
/* Sensor acquisition                                                         */
/* -------------------------------------------------------------------------- */

/** Time between scheduled sensor acquisitions in every controller state.
 *  A lower value reacts faster but increases sensor power cycles and CPU load.
 */
#define DEFROST_CFG_SAMPLE_INTERVAL_MS                    (2000UL)

/** Maximum time one powered sensor attempt may search for a valid frame.
 *  A higher value tolerates a slower sensor but delays fault recognition.
 */
#define DEFROST_CFG_SENSOR_RX_TIMEOUT_MS                  (2000UL)

/** Required sensor-off time between complete power-up attempts/cycles.
 *  A higher value gives a cleaner power reset but lengthens failure recovery.
 */
#define DEFROST_CFG_SENSOR_RETRY_OFF_MS                   (100UL)

/** Time to keep sensor power on after receiving a valid frame.
 *  This implements the sensor protocol's required post-reception delay.
 */
#define DEFROST_CFG_SENSOR_POST_RX_ON_MS                  (10UL)

/** Attempts allowed within one normal-monitoring acquisition.
 *  Increasing this improves retry tolerance but one failed acquisition can
 *  block longer and cause more scheduled samples to be skipped.
 */
#define DEFROST_CFG_NORMAL_ATTEMPTS_PER_ACQUISITION       (3U)

/** Attempts allowed per active-defrost acquisition cycle.
 *  Keep this at one so consecutive-failure abort timing stays near 20 seconds.
 */
#define DEFROST_CFG_ACTIVE_ATTEMPTS_PER_ACQUISITION       (1U)

/** Consecutive failed active-defrost cycles that cause a sensor-fault abort.
 *  A higher value tolerates intermittent communication but leaves temperature
 *  control blind longer. Any valid frame resets the counter to zero.
 */
#define DEFROST_CFG_ACTIVE_CONSECUTIVE_FAILURE_LIMIT      (10U)

/** Maximum duration of one blocking HAL UART poll slice while scanning a
 *  powered sensor stream. A lower value services the watchdog/error path more
 *  often; a higher value reduces polling overhead but blocks longer per call.
 */
#define DEFROST_CFG_SENSOR_UART_POLL_SLICE_MS             (50UL)

/* -------------------------------------------------------------------------- */
/* Diagnostic log                                                             */
/* -------------------------------------------------------------------------- */

/** PA2 software-UART diagnostic baud rate. Raising it shortens blocking log
 *  output but tightens bit-timing tolerance; lowering it increases log latency.
 */
#define DEFROST_CFG_DEBUG_LOG_BAUD                        (115200UL)

/** Stack buffer used to assemble one diagnostic record, including terminator.
 *  Raising it consumes more stack; lowering it may truncate long records.
 */
#define DEFROST_CFG_LOG_BUFFER_BYTES                      (256U)

/* -------------------------------------------------------------------------- */
/* Defrost temperature band and independent safety limits                     */
/* -------------------------------------------------------------------------- */

/** Temperature below which cold exposure accumulates: 0.00 degC.
 *  Raising it counts more operation as frost-forming; lowering it is stricter.
 */
#define DEFROST_CFG_FREEZING_THRESHOLD_CDEGC              (0)

/** Lower band edge and minimum temperature gate for melt success: 2.00 degC.
 *  Raising it demands more warming before success; lowering it permits earlier
 *  success but provides less physical evidence of local melting.
 */
#define DEFROST_CFG_MELT_SUCCESS_TEMP_CDEGC               (200)

/** Upper band edge: 4.00 degC. At/above this value the relay is released so
 *  thermostat-controlled compressor cooling is permitted. Raising it allows
 *  warmer defrost operation; lowering it requests cooling earlier.
 */
#define DEFROST_CFG_UPPER_BAND_TEMP_CDEGC                 (400)

/** Emergency local sensor ceiling: 6.00 degC. Reaching it aborts immediately.
 *  Raising it permits a warmer fault excursion; lowering it aborts sooner.
 */
#define DEFROST_CFG_EMERGENCY_TEMP_CDEGC                  (600)

/** Maximum complete defrost-session duration: 4 hours.
 *  Increasing it allows more melt time but increases maximum warming exposure.
 */
#define DEFROST_CFG_MAX_SESSION_MS                        (14400000UL)

/** Maximum cumulative time above the upper band in one session: 10 minutes.
 *  Increasing it tolerates longer warm exposure; lowering it aborts sooner.
 */
#define DEFROST_CFG_MAX_ABOVE_UPPER_BAND_MS               (600000UL)

/** Delay after a successful defrost before a new session may start: 4 hours.
 *  Increasing it reduces defrost frequency; lowering it permits earlier repeat.
 */
#define DEFROST_CFG_SUCCESS_LOCKOUT_MS                    (14400000UL)

/** Delay after an aborted defrost before a new session may start: 4 hours.
 *  Increasing it avoids repeated failed warming; lowering it retries sooner.
 */
#define DEFROST_CFG_FAILURE_LOCKOUT_MS                    (14400000UL)

/* -------------------------------------------------------------------------- */
/* Conservative cold-exposure scheduling                                     */
/* -------------------------------------------------------------------------- */

/** Normal scheduled-defrost threshold: 5 degC*h = 1,800,000 cdegC*s.
 *  Increasing it requires more frost opportunity; lowering it defrosts sooner.
 */
#define DEFROST_CFG_REQUIRED_EXPOSURE_CDEGC_S             (1800000UL)

/** Minimum exposure for the 24-hour fallback: 1 degC*h = 360,000 cdegC*s.
 *  It prevents elapsed time alone from starting defrost. Raising it makes the
 *  fallback stricter; lowering it permits fallback with less cold exposure.
 */
#define DEFROST_CFG_FALLBACK_MIN_EXPOSURE_CDEGC_S         (360000UL)

/** Elapsed monitoring time at which the minimum-exposure fallback is checked.
 *  Increasing it delays fallback; lowering it checks for fallback sooner.
 */
#define DEFROST_CFG_FALLBACK_ELAPSED_MS                   (86400000UL)

/** Saturation value for the uint32_t cold-exposure accumulator.
 *  It is intentionally equal to the normal trigger threshold; values above the
 *  threshold provide no additional scheduling information.
 */
#define DEFROST_CFG_EXPOSURE_SATURATION_CDEGC_S           (1800000UL)

/* -------------------------------------------------------------------------- */
/* Bootstrap/local-melt pattern                                               */
/* -------------------------------------------------------------------------- */

/** Valid samples used for the robust pre-defrost C_start median.
 *  More samples resist isolated anomalies but delay an otherwise-ready start.
 */
#define DEFROST_CFG_PRESTART_MEDIAN_SAMPLES               (5U)

/** Consecutive valid two-second samples per 10-second median block.
 *  A missed/invalid sample clears the partial block.
 */
#define DEFROST_CFG_BLOCK_SAMPLES                         (5U)

/** Ten-second blocks in the upward-transition window: 6 = 1 minute.
 *  Increasing it accepts slower trends but delays detection; lowering it reacts
 *  faster and provides less temporal evidence.
 */
#define DEFROST_CFG_UPWARD_WINDOW_BLOCKS                  (6U)

/** Required fraction of absolute transition movement that must be upward: 3/4.
 *  A larger fraction demands a cleaner rise; a smaller fraction tolerates more
 *  reversals. Compare by cross multiplication, never floating-point division.
 */
#define DEFROST_CFG_UPWARD_DIRECTION_NUMERATOR            (3U)
#define DEFROST_CFG_UPWARD_DIRECTION_DENOMINATOR          (4U)

/** Consecutive 10-second evaluations required to confirm upward direction.
 *  Increasing it adds persistence and latency; lowering it confirms sooner.
 */
#define DEFROST_CFG_UPWARD_PERSISTENCE_EVALUATIONS        (3U)

/** Ten-second blocks in wet-side stabilization: 30 = 5 minutes.
 *  Increasing it demands longer stability; lowering it terminates sooner.
 */
#define DEFROST_CFG_STABILIZATION_BLOCKS                  (30U)

/** Maximum stabilization range relative to transition rise: 1/4.
 *  A smaller fraction is stricter; a larger fraction accepts more variation.
 */
#define DEFROST_CFG_STABLE_RANGE_NUMERATOR                (1U)
#define DEFROST_CFG_STABLE_RANGE_DENOMINATOR              (4U)

/** Minimum fraction of peak rise retained by final stabilized capacitance: 3/4.
 *  Raising it rejects more post-rise fall; lowering it accepts more fall.
 */
#define DEFROST_CFG_FINAL_RISE_NUMERATOR                  (3U)
#define DEFROST_CFG_FINAL_RISE_DENOMINATOR                (4U)

/** Reversal fraction that cancels the current melt candidate: greater than 1/4.
 *  A smaller fraction resets candidates more readily; a larger fraction is more
 *  tolerant. Cancellation does not abort the complete defrost session.
 */
#define DEFROST_CFG_REVERSAL_NUMERATOR                    (1U)
#define DEFROST_CFG_REVERSAL_DENOMINATOR                  (4U)

/* -------------------------------------------------------------------------- */
/* Trusted ice trend and recovery                                             */
/* -------------------------------------------------------------------------- */

/** Trusted-model ice trigger fraction of the qualified defrost excursion: 3/4.
 *  Raising it waits for a larger contextual drop; lowering it triggers earlier.
 */
#define DEFROST_CFG_TRUSTED_TRIGGER_NUMERATOR             (3U)
#define DEFROST_CFG_TRUSTED_TRIGGER_DENOMINATOR           (4U)

/** One-minute aggregates in the short trend context: 5 = 5 minutes.
 *  Increasing it smooths/slowens the short context; lowering it reacts faster.
 */
#define DEFROST_CFG_SHORT_CONTEXT_MINUTES                 (5U)

/** One-minute aggregates in the long trend context: 60 = 60 minutes.
 *  Increasing it adapts more slowly; lowering it follows context changes sooner.
 */
#define DEFROST_CFG_LONG_CONTEXT_MINUTES                  (60U)

/** Consecutive minutes short context must remain below long context.
 *  Increasing it rejects brief drops; lowering it recognizes trends sooner.
 */
#define DEFROST_CFG_DOWNWARD_TREND_PERSISTENCE_MIN        (15U)

/** Consecutive minutes the trusted 3/4 excursion trigger must remain true.
 *  Increasing it reduces brief triggers; lowering it starts defrost sooner.
 */
#define DEFROST_CFG_TRUSTED_TRIGGER_PERSISTENCE_MIN       (5U)

/** Time a qualified earlier downward trend remains usable: 120 minutes.
 *  This lets a later low-slope ice plateau retain its preceding trend evidence.
 */
#define DEFROST_CFG_DOWNWARD_TREND_MEMORY_MIN             (120U)

/** Credible thermostat cooling/warming cycles required after a disturbance.
 *  Increasing it protects learning longer; lowering it resumes learning sooner.
 */
#define DEFROST_CFG_RECOVERY_CREDIBLE_CYCLES              (2U)

/* -------------------------------------------------------------------------- */
/* Compile-time relationship checks                                           */
/* -------------------------------------------------------------------------- */

#if (DEFROST_CFG_MELT_SUCCESS_TEMP_CDEGC >= DEFROST_CFG_UPPER_BAND_TEMP_CDEGC)
#error "Melt-success/lower-band temperature must be below upper-band temperature"
#endif

#if (DEFROST_CFG_UPPER_BAND_TEMP_CDEGC >= DEFROST_CFG_EMERGENCY_TEMP_CDEGC)
#error "Upper-band temperature must be below emergency temperature"
#endif

#if (DEFROST_CFG_FALLBACK_MIN_EXPOSURE_CDEGC_S == 0UL)
#error "Fallback exposure must be nonzero; elapsed time alone cannot trigger defrost"
#endif

#if (DEFROST_CFG_FALLBACK_MIN_EXPOSURE_CDEGC_S > DEFROST_CFG_REQUIRED_EXPOSURE_CDEGC_S)
#error "Fallback minimum exposure cannot exceed the normal exposure threshold"
#endif

#if (DEFROST_CFG_EXPOSURE_SATURATION_CDEGC_S < DEFROST_CFG_REQUIRED_EXPOSURE_CDEGC_S)
#error "Exposure saturation must reach the normal exposure threshold"
#endif

#if (DEFROST_CFG_UPWARD_DIRECTION_NUMERATOR > DEFROST_CFG_UPWARD_DIRECTION_DENOMINATOR)
#error "Upward direction fraction cannot exceed one"
#endif

#if (DEFROST_CFG_STABLE_RANGE_NUMERATOR > DEFROST_CFG_STABLE_RANGE_DENOMINATOR)
#error "Stable-range fraction cannot exceed one"
#endif

#if (DEFROST_CFG_FINAL_RISE_NUMERATOR > DEFROST_CFG_FINAL_RISE_DENOMINATOR)
#error "Final-rise fraction cannot exceed one"
#endif

#if (DEFROST_CFG_REVERSAL_NUMERATOR > DEFROST_CFG_REVERSAL_DENOMINATOR)
#error "Reversal fraction cannot exceed one"
#endif

#if (DEFROST_CFG_TRUSTED_TRIGGER_NUMERATOR > DEFROST_CFG_TRUSTED_TRIGGER_DENOMINATOR)
#error "Trusted-trigger fraction cannot exceed one"
#endif

#if ((DEFROST_CFG_UPWARD_DIRECTION_DENOMINATOR == 0U) || \
     (DEFROST_CFG_STABLE_RANGE_DENOMINATOR == 0U) || \
     (DEFROST_CFG_FINAL_RISE_DENOMINATOR == 0U) || \
     (DEFROST_CFG_REVERSAL_DENOMINATOR == 0U) || \
     (DEFROST_CFG_TRUSTED_TRIGGER_DENOMINATOR == 0U))
#error "Ratio denominators must be nonzero"
#endif

#if ((DEFROST_CFG_SAMPLE_INTERVAL_MS == 0UL) || \
     (DEFROST_CFG_SENSOR_RX_TIMEOUT_MS == 0UL) || \
     (DEFROST_CFG_SENSOR_UART_POLL_SLICE_MS == 0UL) || \
     (DEFROST_CFG_NORMAL_ATTEMPTS_PER_ACQUISITION == 0U) || \
     (DEFROST_CFG_ACTIVE_ATTEMPTS_PER_ACQUISITION == 0U) || \
     (DEFROST_CFG_ACTIVE_CONSECUTIVE_FAILURE_LIMIT == 0U) || \
     (DEFROST_CFG_DEBUG_LOG_BAUD == 0UL) || \
     (DEFROST_CFG_LOG_BUFFER_BYTES < 2U))
#error "Sensor timing and failure-limit parameters must be nonzero"
#endif

#if ((DEFROST_CFG_NORMAL_ATTEMPTS_PER_ACQUISITION > 255U) || \
     (DEFROST_CFG_ACTIVE_ATTEMPTS_PER_ACQUISITION > 255U))
#error "Sensor attempt counts must fit in uint8_t"
#endif

#if (DEFROST_CFG_SENSOR_UART_POLL_SLICE_MS > DEFROST_CFG_SENSOR_RX_TIMEOUT_MS)
#error "UART poll slice cannot exceed the complete sensor receive timeout"
#endif

#endif /* DEFROST_CONFIG_H */

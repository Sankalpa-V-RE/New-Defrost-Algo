# Fridge Defrost Controller: Design Plan

## 0. Document status and rules

This is a clean-slate project. Do not import code, constants, algorithms, or assumptions from other projects.

Current phase: requirements and safety planning. No production algorithm, thresholds, timing values, or compressor-protection values have been approved. Values marked **provisional** are starting points for experiments, not production safety claims.

Naming rules:

- Use descriptive names rather than single-letter or unexplained abbreviated names.
- Relay wording must be explicit:
  - **Relay energized = compressor power interrupted = defrost/warming permitted.**
  - **Relay de-energized = compressor power available = refrigerator controls cooling normally.**
- Record decisions and their reasons. Keep unresolved questions visible instead of silently choosing an answer.

### 0.1 STM32CubeIDE file-ownership rule

- Treat `DEFROST_controller.ioc` as the source of truth for pin, clock, peripheral, and generated initialization configuration. Prefer changing it through the Cube configuration editor and regenerate code.
- In Cube-generated `.c` and `.h` files, project-owned application code may be added only inside matching `/* USER CODE BEGIN ... */` and `/* USER CODE END ... */` regions unless the user explicitly authorizes a generated-section change.
- Place substantial application logic in separately created project-owned modules under `Core/Src` and `Core/Inc`; keep `main.c` orchestration small.
- Do not edit `Drivers/`, `CMSIS`, startup code, linker scripts, `.project`, `.cproject`, or other tool-managed content unless the user explicitly authorizes that specific change.
- Before regeneration, verify `ProjectManager.KeepUserCode=true`. After regeneration, review changes to ensure user regions and project-owned modules were preserved.
- The 2026-10-15 clock conversion is an explicit exception: the `.ioc` clock source and generated `SystemClock_Config()` were synchronized from HSE 12 MHz to HSI48 divided by 4. Future clock changes return to the `.ioc`-then-regenerate workflow.

## 1. Purpose and system boundary

The product is an add-on, sensor-driven defrost controller. It controls only the mains supply path to the compressor; it does not replace the refrigerator thermostat or its normal controls.

During normal operation, the relay is de-energized and the refrigerator works independently. The controller periodically measures temperature and capacitance at the location where ice is expected to form first. It must infer real ice accumulation from changes and trends rather than depend on one universal absolute capacitance threshold.

When there is sufficient evidence of ice, the controller energizes the relay and prevents compressor operation. There is no defrost heater. Heat entering the refrigerator raises the temperature and allows ice to melt. The controller continues to observe temperature, capacitance, elapsed time, and faults, then de-energizes the relay to return compressor control to the refrigerator.

The controller does not need to know whether the refrigerator thermostat was requesting compressor operation. It only inhibits compressor operation during a defrost attempt.

## 2. Hardware baseline

| Item | Detail |
|---|---|
| MCU | STM32C011F6Px, Cortex-M0+, up to 32 KB flash, 6 KB SRAM with hardware parity, no FPU |
| EEPROM | 64 Kbit / 8 KB I2C EEPROM, 32-byte page, more than 4 million write cycles |
| Sensor | One processed temperature + capacitance module at the location where ice forms first |
| Actuator | Relay interrupts mains power to the compressor only |
| Other I/O | LED, button, buzzer, door input, SWD |
| Clock | HSI48 divided by 4 gives a 12 MHz system clock. The fitted 12 MHz crystal is not the firmware clock source. |
| Debug log | Transmit-only software UART on PA2 at 115200 baud |

Relay/mains hardware review, MOSFET gate pull-down verification, contact/inrush rating, suppression, fuse, clearance, creepage, and other electrical-safety validation are outside the current firmware scope by user direction. Firmware must still preserve the documented fail-safe output behavior, but this plan makes no electrical hardware-safety claim.

### 2.1 Pin map

| Pin | Net | Use |
|---|---|---|
| PB7 | I2C1_SDA | EEPROM data |
| PB6 | I2C1_SCL | EEPROM clock |
| PC14 / PC15 | RCC_OSCX_IN / RCC_OSCX_OUT | Fitted 12 MHz crystal; not configured by firmware and not selected as system clock |
| PF2 | NRST | Reset |
| PA0 | BUZZER | Buzzer |
| PA1 | RELAY | Active-high low-side relay-coil control |
| PA2 | softUART_TX | Software-UART log output, 115200 baud |
| PA3 | Not configured | Unused |
| PA4 | USART2_TX | Sensor single-wire data, AF1, open-drain, internal pull-up, half-duplex, 9600 baud |
| PA5 | SENSOR_TRIG | Available active-low open-drain trigger, but not used by the current power-triggered sensor transaction |
| PA6 | CAP_SENSOR_EN | Push-pull sensor power enable; HIGH powers the sensor |
| PA7 | LED | LED |
| PA8 | USER_BTN | Button |
| PA11 [PA9] | DOOR_SENSOR | Future implementation; not read or used by the current algorithm. Bracketed name is schematic notation. |
| PA12 [PA10] | Not configured | Unused; formerly a test point |
| PA13 | DEBUG_SWDIO | SWD debug |
| PA14 | DEBUG_SWCLK / BOOT0 | SWD clock and BOOT0 sharing |

## 3. Safety invariants

1. Reset, loss of MCU power, unconfigured GPIO, firmware failure, and watchdog recovery must default PA1 to the state that de-energizes the relay and returns compressor control to the refrigerator.
2. Normal cooling must never depend on successful sensor communication, EEPROM access, logging, or completion of the defrost algorithm.
3. Logging must never delay or change relay safety handling.
4. Invalid, missing, implausible, or stale sensor data must not extend defrost. A sensor fault during defrost de-energizes the relay and aborts the attempt.
5. The relay must always be de-energized after the maximum defrost time or a separate emergency upper-temperature ceiling is reached, regardless of capacitance. The 4-degree band edge is not this emergency ceiling.
6. A defrost decision must be persistent and debounced. A single measurement cannot energize the relay.
7. Rapid relay cycling must be prevented by configurable state-duration and lockout parameters.
8. EEPROM corruption or loss must fall back to conservative compiled defaults and normal refrigerator control.


## 4. Serial interfaces and timing

USART2 receives sensor data on PA4 at 9600 baud. PA2 logs independently through a transmit-only software UART at 115200 baud.

The production log transmitter must use timer-based timing and a bounded buffer. Sensor reception, watchdog service, timekeeping, and relay safety have priority. Logs may be paused or dropped under load; safety work may not be delayed to preserve logs. Timing and interrupt-load measurements are required before enabling verbose logs in defrost tests.

The regular controller CSV record is ordered as follows:

`ms, sequence, sensor_status, temperature_c, capacitance_pf, defrost_active, controller_state, relay_energized, event, cold_exposure_cdegc_s, session_elapsed_ms, above_upper_ms, lockout_remaining_ms, consecutive_failures, attempts, crc_errors, frame_errors, uart_errors, acquisition_ms, frame_hex`

- `ms` is the monotonic timestamp at acquisition start and `sequence` increments once per scheduled acquisition record. Both are retained so resets, missing lines, and timing gaps are visible.
- `temperature_c` is signed Celsius with exactly two decimal places. The sensor transports signed hundredths of a degree, so a raw value such as `915` is logged as `9.15`, never as `915 C`.
- `capacitance_pf` is the decoded big-endian `uint16_t` sensor value in picofarads.
- `defrost_active` is a direct `0/1` session flag. `controller_state` distinguishes monitoring, compressor-inhibited defrost, cooling-permitted defrost, recovery, lockout, and fault states. `relay_energized` is the actual commanded relay output (`1` means compressor power is interrupted).
- `event` is normally `NONE` and names transitions such as defrost start, band change, melt success, timeout, over-temperature, sensor abort, reset recovery, or model invalidation.
- Exposure, session, upper-band, lockout, failure, acquisition, and protocol-error fields make state-machine timing and failures diagnosable. `frame_hex` retains the validated eight-byte frame for interface diagnosis; the redundant raw-temperature decimal column is intentionally omitted.

Invalid sensor records leave temperature, capacitance, and frame fields empty rather than repeating stale values. Logging is diagnostic only and is never consumed by the control algorithm or used to derive/tune it.

The measurement-only firmware temporarily uses a blocking, SysTick-cycle-timed PA2 transmitter after sensor reception is complete and sensor power is off. It disables interrupts for approximately one 8N1 character at a time. This is acceptable only while the relay is permanently de-energized and must be replaced or revalidated before automatic defrost control is introduced. Verify its 115200-baud waveform and accumulated timing error with test equipment.

## 5. Sensor protocol and acquisition

Powering the sensor through `CAP_SENSOR_EN` starts its transmission. It begins streaming frames approximately 50 ms after power-up and may transmit multiple frames while powered.

### 5.1 Frame

The frame is eight bytes:

| Byte | Meaning |
|---|---|
| 0 | Start marker `0xAA` |
| 1-2 | Processed signed temperature times 100, big-endian `int16_t` |
| 3-4 | Processed capacitance, big-endian `uint16_t` picofarads |
| 5-6 | Received CRC, most-significant byte first |
| 7 | End marker `0xAC` |

The sensor temperature field is in signed hundredths of a degree Celsius. Control calculations retain that integer centi-degree representation; human-readable UART output places the decimal point by dividing by 100. Capacitance is already processed picofarads and needs no scaling. Every multi-byte field uses big-endian byte order.

CRC is CRC-16/CCITT-FALSE over bytes 1 through 4 only. The received value is assembled as:

```c
uint16_t received_crc = ((uint16_t)data[5] << 8) | data[6];
uint16_t calculated_crc = CRC16_CCITT_FALSE(&data[1], 4);
```

### 5.2 Proposed acquisition sequence

1. Ensure `CAP_SENSOR_EN` is LOW.
2. Configure the unpowered-sensor PA4 state so it cannot back-power the sensor; this must be verified electrically.
3. Clear stale USART flags and buffered bytes.
4. Configure PA4 for USART2 half-duplex reception and arm reception before sensor power is applied.
5. Set `CAP_SENSOR_EN` HIGH.
6. For up to the configurable receive timeout, initially **2000 ms**, scan for `0xAA`, collect the remaining seven bytes, and check the end marker and CRC.
7. A corrupt frame does not immediately end the acquisition. Continue searching the stream for the first complete valid frame until the deadline.
8. After a valid frame is received, keep the sensor powered for **10 ms**, then set `CAP_SENSOR_EN` LOW.
9. If the acquisition deadline expires without a valid frame, power the sensor off, wait a configurable retry interval, and repeat the complete power-up sequence.
10. Bound the number of retries with a configurable value. Exhausted retries produce a sensor fault; during defrost this aborts defrost and de-energizes the relay.

One exhausted acquisition cycle increments a consecutive-failure counter; any valid frame resets it. The initial automatic-control limit is **10 consecutive failed acquisition cycles** and remains a named rebuild-time parameter. Normal monitoring initially permits three complete power-up attempts per acquisition, separated by 100 ms powered off. Active defrost permits only one 2000 ms attempt per acquisition cycle, with at least 100 ms powered off before another cycle; therefore 10 continuously failed active-defrost cycles abort in approximately 20 to 21 seconds rather than leaving temperature protection blind for about one minute. Reaching the limit aborts the session and de-energizes the relay.

The receiver must recover from noise, partial frames, repeated start markers, bad end markers, CRC failures, UART errors, and a stream that begins near the 50 ms startup boundary.

### 5.3 Measurement-only firmware cadence and output

During initial data collection, schedule one acquisition start every **2000 ms**. A healthy acquisition normally finishes well before the next slot. If timeout and power-cycle retries overrun one or more slots, skip the missed slots rather than perform a burst of delayed acquisitions.

Log one CSV record per acquisition on PA2 at 115200 baud with timestamp, sequence, status, decoded temperature, decoded capacitance, attempt count, CRC/framing/UART error counts, acquisition duration, and the validated raw frame in hexadecimal. Failed acquisitions leave decoded values and frame empty rather than reporting stale data.

The measurement-only firmware must keep the relay de-energized and contains no automatic ice decision or defrost action.

### 5.4 Sensor physical-response contract

The following are sensor-design inputs supplied for algorithm specification; they are not conclusions derived from captured UART logs:

- Liquid water causes the reported capacitance to rise.
- When liquid water freezes, the reported capacitance falls.
- When ice melts back to liquid water, the expected transition direction is therefore a capacitance rise.
- At comparable conditions, a fully ice-covered sensor has higher capacitance than a completely dry sensor. Together, the supplied endpoint ordering is **dry < ice < liquid water**.
- In the installed refrigerator the sensor is expected never to be dry. Within the relevant in-service wet/ice operating range, the lowest contextual capacitance condition corresponds to thick ice.
- Direct temperature dependence of the capacitance output is negligible.
- Temperature accuracy is high and sensor thermal lag is negligible for this application.
- Sensor noise is sufficiently low that it is not expected to affect the capacitance trend used by the algorithm; no noise-derived ice threshold is required.
- Contamination or a persistent water film can permanently shift the capacitance baseline.

The endpoint ordering among **dry**, **frozen ice**, and **liquid water** is now specified, but absolute values and separations are intentionally not. The dry endpoint is outside expected installed operation. As ice accumulates over the wet sensor, capacitance falls until thick ice fully covers the sensing region; at that point the reading approaches a lower plateau and appears to stop falling. Reaching this plateau means detection was too late for the intended control objective. Normal defrost triggering must therefore use the earlier persistent downward deviation/trend from the qualified wet, low-ice contextual reference, supported by frost opportunity and persistence. Plateau detection may serve only as an overdue backstop, never as the normal target. Capacitance still must not be converted into a quantitative ice-thickness or ice-mass estimate. "Lowest means thick ice" is a contextual ordering, not a universal raw threshold. Valid numerical ranges, saturation behavior, and invalid/sentinel outputs remain open; sensor noise is declared negligible for algorithm design.

In this plan, a **trusted reference** means a qualified wet, low-ice post-defrost operating reference; it is never a dry reference. A persistent water film or contamination may be part of the current qualified baseline, provided the learning gates and recovery rules accept it. The algorithm must detect later phase transitions relative to that shifted context rather than trying to force the reference toward the lower but irrelevant dry endpoint.

## 6. Operating-state model

The state machine must separate at least these responsibilities:

1. **Boot self-check:** establish safe outputs first, determine reset cause, validate configuration, and leave the relay de-energized.
2. **Normal monitoring:** permit refrigerator operation and collect periodic sensor measurements.
3. **Ice-candidate confirmation:** require sustained multi-sample evidence and reject known disturbances.
4. **Defrost active:** regulate within the configured temperature band by alternating between compressor inhibition and compressor-permitted substates, while continuously monitoring learned melt evidence and all hard limits.
5. **Defrost completion/recovery:** de-energize the relay, observe the post-defrost response, discard or reset event-local comparison state as specified, and prevent immediate retriggering.
6. **Fault/disabled automatic defrost:** keep the relay de-energized while reporting diagnostics.

For coincident events, evaluate transitions in this priority order: emergency temperature ceiling; sensor/fault abort; maximum total defrost duration; maximum cumulative time above the upper band; qualified melt success; temperature-band relay transition; new ice/defrost trigger. A higher-priority event always wins and safety/fault handling cannot be delayed by learning or relay-control logic.

Any unsuccessful defrost immediately de-energizes the relay, rejects every learning candidate from that session, clears session-local melt state, and starts a configurable failed-attempt lockout initially **4 hours**. Sensor-fault recovery additionally requires valid communication to be restored. When the lockout ends, the aborted ice request is not executed as a pending request; a new session requires newly qualified ice evidence or renewed fallback eligibility.

## 7. Defrost control policy

### 7.1 Initial policy choice

The first design will use **learned melt evidence plus hysteretic temperature-band control and independent hard termination limits**.

Capacitance and temperature history will provide evidence that ice has melted. Temperature alone will not declare complete melting. Passing 0 degrees Celsius at the single sensor proves only that the sensor location is above freezing; ice may remain elsewhere because of thermal gradients, placement, water/ice mixtures, and differing heat paths.

Successful defrost terminates immediately when the learned melt algorithm qualifies local melt and the minimum melt-success temperature condition is satisfied. Unsuccessful termination/abort occurs when any independent hard abort is reached:

- Total defrost time reaches the configured maximum.
- Sensor data becomes unavailable, invalid, implausible, or stale beyond its allowed limit.
- A reset or other safety fault occurs.
- A future separately configured emergency upper-temperature ceiling or over-temperature-duration rule is reached.

After termination, the relay is de-energized. While defrost remains active and melt is not yet qualified, use the 2-to-4-degree band controller defined below.

Only termination by qualified learned melt evidence at or above the configured minimum melt-success temperature is classified as **successful defrost**. Maximum-time, emergency over-temperature, sensor-fault, reset, or internal-fault termination is an abort/unsuccessful outcome and cannot update trusted learning state. Crossing 4 degrees is a band-control event, not termination.

### 7.2 Provisional experimental limits

- Lower defrost-band edge and minimum temperature for melt success: initial value **2 degrees Celsius**.
- Upper defrost-band edge: initial value **4 degrees Celsius**. Reaching it does not abort defrost; it de-energizes the relay and permits the refrigerator thermostat to request compressor cooling.
- Maximum total defrost-stage duration parameter: initial value **4 hours**. The value is rebuild-time configurable, but the configured limit is always mandatory and defrost must never continue beyond it.
- Emergency sensor-temperature ceiling parameter: initial value **6 degrees Celsius**. Reaching it aborts defrost immediately and leaves the relay de-energized.
- Maximum cumulative time above the upper band during one defrost session: initial value **10 minutes**. Accumulate only while sensor temperature is above the configured upper band, pause rather than reset while at/below it, and reset when the defrost session ends. Reaching the configured duration aborts defrost and leaves the relay de-energized.
- Minimum interval after a successfully completed defrost before a new defrost session may begin: initial value **4 hours**. This post-defrost lockout does not run as an in-session relay dwell and does not prevent repeated 2-to-4-degree cycling during an active defrost.

All temperature/time values are rebuilt into firmware during experiments and remain named parameters. Configuration validation must require `lower_band < upper_band < emergency_ceiling`, all duration parameters must be positive, and accumulator arithmetic must saturate safely. None is production-approved until cabinet-wide temperature and food-safety testing is complete.

### 7.3 Hysteretic defrost-band control

The controller cannot command the compressor ON. It can only de-energize the relay and make compressor power available to the refrigerator's own thermostat. Therefore, the upper-band action is **cooling permitted**, not guaranteed cooling.

While the overall defrost session is active and learned melt success has not occurred:

- At or below the configured lower band edge, initially 2 degrees Celsius, energize the relay and inhibit compressor operation so passive warming/melting can continue.
- At or above the configured upper band edge, initially 4 degrees Celsius, de-energize the relay and permit normal thermostat-controlled compressor operation.
- Between the two band edges, retain the current relay substate; do not chatter between states.
- The total maximum-defrost-duration timer continues across both substates and is never reset or paused by temperature cycling or door activity.
- The upward switch at the upper band makes cooling available immediately. If temperature later reaches the lower band while the same defrost session is still active, the relay may re-energize and inhibit the compressor again. The 4-hour post-defrost lockout does not apply inside the active session.
- Melt detection continues in both substates. As soon as learned capacitance melt evidence is qualified and temperature is at or above the lower band edge, terminate the defrost session and leave the relay de-energized.

Repeated 2-to-4-degree cycling is intentional during one active defrost. The 4-hour timer begins only after a successful defrost session has ended and prevents a new defrost session from starting during that lockout. It is not a minimum cooling-permitted interval within a defrost.

No additional minimum relay-energized dwell or general minimum relay-state dwell is required. The 2-to-4-degree hysteresis is the normal anti-chatter mechanism. Qualified melt success, the 4-degree upper-band transition, and every fault or safety abort take effect immediately and are never delayed to satisfy a dwell timer.

If temperature remains above the upper band because the thermostat does not request cooling, the compressor is unavailable, or the door remains open, this controller cannot guarantee band regulation. Abort immediately at the configured emergency ceiling, initially 6 degrees Celsius, or when cumulative time above the upper band reaches its configured limit, initially 10 minutes. The configured maximum defrost duration remains the final independent time limit.

### 7.4 Single-sensor limitation

The sensor is placed where ice forms first, not necessarily at the warmest food-storage location. Its reading cannot prove that all food locations remain at or below a safety limit. A local reading of 4 degrees Celsius may coexist with a higher temperature elsewhere.

Required mitigation during development:

- Use multiple independent calibrated reference temperature loggers at representative warm and cold cabinet locations.
- Measure worst-case spatial temperature differences for empty, normally loaded, and heavily loaded refrigerators.
- Include door events, hot loads, ambient extremes, and long defrost attempts.
- Record time-temperature exposure, not only peak sensor temperature.
- Derive the production sensor ceiling with an evidence-based safety margin below the permitted cabinet/food limit.

If the product requirement is to guarantee the maximum cabinet or food temperature using the controller itself, the present single-sensor hardware is insufficient; add a separate representative cabin/food-zone temperature sensor or change the requirement.

Official food-safety guidance commonly requires a refrigerator to remain at or below 4 degrees Celsius / 40 degrees Fahrenheit throughout the unit. This project must not treat a locally measured 4-degree value as proof of compliance.

## 8. Compressor and relay protection parameters

The controller does not measure actual compressor operation and does not need that information for its main function. It times only when compressor power is permitted or interrupted.

Provide named, compile-time experimental parameters for at least:

- Minimum confirmed evidence duration before starting defrost.
- Maximum total defrost duration.
- Lower and upper defrost-band temperatures.
- Emergency over-temperature ceiling and cumulative maximum time above the upper band.
- Minimum interval after a successfully completed defrost before another defrost may begin: initial value **4 hours**.
- Post-defrost observation/recovery interval.
- Lockout after an aborted or failed defrost.
- Sensor receive timeout.
- Sensor power-cycle retry delay and maximum retry count.
- Maximum consecutive sensor failures before automatic defrost is disabled.
- Algorithm filter windows, confidence thresholds, hysteresis, and persistence counts.

Parameter values will initially be changed by rebuilding firmware. A serial command interface and EEPROM-editable configuration are not currently required.

All rebuild-time defrost algorithm and safety parameters must be declared in the project-owned `Core/Inc/defrost_config.h`; do not scatter numeric policy constants through source files or place them in Cube-generated sections. Each declaration must document its unit, purpose, the behavioral effect of increasing/decreasing it, and any relationship constraints. The header must enforce relationships such as lower band below upper band below emergency ceiling and nonzero fallback exposure with compile-time checks. Peripheral pins and hardware configuration remain owned by `DEFROST_controller.ioc`, not this parameter header.

The implementation is intended to actuate the real relay according to the approved state machine; a permanently forced-de-energized/shadow-only build is not an acceptable final implementation. Fail-safe startup, reset, sensor-fault, timeout, and internal-fault paths must nevertheless de-energize the relay.

## 9. Reset, watchdog, and power-loss policy

The initial safety policy is:

- Power loss or brownout: relay de-energizes; abandon the active defrost.
- Unknown reset cause: relay remains de-energized; abandon the active defrost.
- Watchdog or software reset: initially also abandon defrost and return control to the refrigerator.
- After restart, require stable operation and the configured lockout/confirmation rules before another defrost attempt.

This is intentional. Software cannot simultaneously guarantee immediate fail-safe relay release and uninterrupted compressor inhibition across every MCU reset. Reasserting the relay after boot could briefly restore compressor power and then interrupt it again.

A hardware timed ride-through circuit is a possible future option. It could hold the relay energized through a short MCU reset but release it after a bounded timeout if firmware does not recover. This requires a hardware change and explicit acceptance that failure release is delayed by that timeout. Do not rely on relay mechanical release timing as a safety mechanism.

The watchdog is required, but watchdog recovery must lead to the safe de-energized state rather than automatically resuming defrost.

## 10. EEPROM policy

Parameters are compiled into firmware during the experimental phase. Do not persist them merely for convenience.

EEPROM use must be justified item by item. A single permanently conditioned installation profile is not sufficient, but a versioned trusted reference produced by the approved guarded online-learning algorithm may be considered. Possible future uses also include bounded diagnostic counters or an interrupted-defrost marker. Any stored structure must use versioning, integrity checking, atomic/dual-record updates where needed, and wear-aware write frequency. Loss or corruption of EEPROM must not prevent normal refrigerator operation.

No EEPROM-dependent algorithm has yet been approved.

## 11. Algorithm-development method

### 11.1 Diagnostic-log boundary

UART measurement logs are for communication bring-up, frame decoding, fault diagnosis, and human visibility only. Recorded log data must not be used to select algorithm features, derive thresholds, tune windows or confidence values, train a model, identify ice/melt patterns, or justify conclusions about algorithm correctness.

The future controller may use bounded sensor history and guarded online learning to adapt to current conditions. That in-device history is operational state, not an offline dataset. Captured UART logs remain prohibited as algorithm-development or tuning data.

Before automatic relay control is enabled, the algorithm, parameters, state transitions, and safety bounds must be specified independently and version-frozen. Subsequent testing must use acceptance criteria written before the test. A failed test invalidates the frozen design; recorded logs must not be mined to retrofit or tune it unless the user explicitly changes this rule.

The production algorithm must identify **temporal and state-transition patterns**, not compare raw temperature or capacitance against installation-specific ice values. Absolute capacitance, absolute change magnitude, and fixed dry/iced values may vary substantially with refrigerator construction, sensor mounting, environment, contamination, age, and location.

Customer commissioning and reliance on one conditioned reference learned from an assumed clean installation are rejected. Fridge location, ambient conditions, loading, airflow, and usage are not controlled, so an unguarded persistent site profile can normalize the wrong condition or drift into accepting ice as normal. Online learning is allowed only when it adapts to current context and protects a trusted wet, low-ice reference with explicit qualification, freeze, rate-limit, invalidation, and recovery rules.

The selected high-level architecture is a guarded, context-adaptive detector with a conservative scheduled bootstrap. Its detailed equations and constants are not yet approved. The architecture must obey these constraints:

- Do not require customer commissioning or a factory/site-specific capacitance profile.
- Maintain separate short-term context state and slow trusted wet, low-ice reference state; do not let an unqualified short-term condition overwrite the trusted state.
- Permit trusted-reference updates only from qualified post-defrost recovery or another independently justified clean-state gate.
- Freeze learning during suspected ice, defrost, sensor faults, inferred door/hot-load-like disturbances, and unstable recovery.
- Bound update magnitude and rate, retain confidence/age metadata, and invalidate or re-bootstrap models after discontinuities that make them untrustworthy.
- Short-lived comparisons within the current cooling, warming, or deliberate-defrost event may be used and reset when the event becomes invalid.
- Prefer dimensionless direction, ordering, persistence, and bounded within-event change over installation-specific absolute capacitance.
- Condition capacitance behavior on temperature history rather than treat capacitance independently.
- Use direction, rate, persistence, sequence, and physically justified transition behavior.
- Accumulate confidence from multiple agreeing features; no single sample or feature may confirm ice or melt completion.
- Use separate entry and exit confidence with hysteresis so the state cannot chatter.
- Reset or invalidate event-local comparison state during sensor faults, inferred door/hot-load-like disturbances, resets, and other broken observation sequences; preserve a persistent trusted reference only if its version, integrity, age, and confidence checks pass.
- Keep hard safety ceilings for time, temperature, faults, and relay activity independent from the pattern detector.

For each qualified temperature-region and thermal-direction context, the trusted model will represent at least wet/low-ice reference capacitance, confidence, and age. A current measurement is first interpreted using a signed contextual residual rather than raw capacitance:

`residual = measured_capacitance - contextual_reference_capacitance`

Sensor noise is declared negligible, so the algorithm will not normalize this residual by a learned noise or ordinary-variation estimate. The selected primary adaptive magnitude scale is the **qualified defrost-cycle excursion**, not the wet-to-initial-freeze phase swing.

Before a qualified defrost begins, capture a robust pre-defrost capacitance. After local melting is qualified, capture a candidate wet capacitance:

`reference_excursion = qualified_after_melt_capacitance - qualified_before_defrost_capacitance`

During the next accumulation period:

`current_drop = trusted_wet_reference - measured_capacitance`

Trigger comparison uses a configurable fraction of the trusted excursion without division:

`current_drop * fraction_denominator >= trusted_reference_excursion * fraction_numerator`

The initial configured fraction is **0.75**, represented exactly as numerator 3 and denominator 4. It is an operational trigger point, not a physical ice-coverage percentage. A reference excursion below its minimum valid magnitude is ineligible; capacitance-based triggering is then unavailable and the conservative cold-exposure fallback remains authoritative. The wet-to-initial-freeze swing may still contribute optional freezing/recovery event evidence, but it is not stored or required as the accumulation denominator.

Reference updates use two stages. A newly observed after-melt value and excursion are **candidates** only. A candidate may be committed to the trusted model only after the defrost ends as qualified local melt success, recovery quarantine completes, subsequent thermostat cycles are credible, integrity/context checks pass, and bounded-update rules accept it. Temperature-ceiling, maximum-time, sensor-fault, reset, or otherwise uncertain defrost outcomes cannot update the trusted reference. The first bootstrap candidate has limited confidence and the conservative schedule remains authoritative until later qualification confirms the model.

The supplied sensor response gives physically meaningful endpoint and phase-transition directions. From a dry condition, full ice coverage has a higher endpoint; liquid water has a still-higher endpoint, although dry is not expected in service. A water-arrival/wetting event moves capacitance upward; freezing and subsequent ice accumulation move it downward toward the thick-ice plateau; melting moves it upward again. The algorithm should trigger from a qualified persistent downward contextual deviation and trend while slope remains meaningfully negative, before it approaches the low-slope plateau. These transitions may contribute to confidence only when their temperature ordering, frost opportunity, persistence, context, and disturbance checks also agree. A high value alone cannot distinguish water from ice, and a downward transition alone cannot distinguish freezing, gradual accumulation, drainage, removal, contamination shift, or sensor drift. Negligible sensor noise removes the need for a noise-derived threshold but does not remove data-validity checks, persistence, hysteresis, or arithmetic denominator guards.

### 11.2 Operating context, recovery, and bootstrap

The refrigerator does not maintain one stable temperature. Its thermostat creates recurring cooling and warming legs, producing an approximately sawtooth temperature trajectory. Therefore, **normal operation** means credible recurring thermostat-driven cycles, not a flat temperature. The contextual model must at least distinguish temperature region and thermal direction; the same temperature on a cooling leg and a warming leg may legitimately have different capacitance behavior.

**Recovery** is a quarantined transient after deliberate defrost or a major inferred disturbance. After defrost terminates and compressor power is made available again, temperature returns from the warmed condition toward recurring thermostat cycling; liquid water may drain, redistribute, or refreeze, and capacitance may still be settling. A similar recovery interval may follow temperature/capacitance behavior consistent with a door/hot-load-like event, but the physical door input is not used. Recovery is not a clean reference by definition. Trusted-reference learning remains disabled until predefined qualification rules show valid sensor data, absence of known disturbance, and a sufficient number of credible alternating cooling/warming legs. The required cycle count and consistency bounds remain open parameters.

The selected startup approach is **conservative scheduled bootstrap**:

1. Start with no trusted wet, low-ice reference model and keep the relay de-energized so normal refrigerator control remains available.
2. Monitor valid temperature/capacitance and accumulate a conservative cold-exposure condition while also enforcing a maximum elapsed-time boundary; do not make a learned ice decision yet. The combination must be specified so elapsed time alone cannot force an unjustified warming event when credible frost-forming exposure has not occurred.
3. When the bootstrap condition is satisfied, capture the qualified pre-defrost capacitance and perform one bounded defrost using independent time, temperature, sensor-fault, and relay-protection limits.
4. If local melt success is qualified, capture candidate after-melt wet capacitance and candidate defrost-cycle excursion; an abort cannot produce reference candidates.
5. Treat the subsequent interval as recovery quarantine; do not immediately declare its data clean.
6. Verify the candidate against qualified recurring cooling/warming cycles after recovery.
7. Commit the candidate only after its predefined confidence, consistency, and bounded-update requirements pass. If bootstrap defrost aborts or recovery never qualifies, retain no trusted reference and enter the specified conservative fallback behavior.

The degree-time calculation, scheduled-due logic, initial melt detector, trend windows, and disturbance quarantine are selected below. Learned-model consistency and bounded candidate-commit equations remain to be defined for the later adaptive stage.

### 11.3 Selected cold-exposure concept

Cold exposure will use temperature-weighted degree-time rather than simple time below a threshold. For each valid observation interval:

`exposure_increment = max(0, freezing_guard_temperature - measured_temperature) * elapsed_interval`

The accumulator uses unsigned saturating fixed-point arithmetic in **centi-degree-Celsius-seconds**. For each valid on-schedule two-second sample:

`increment = max(0, 0 - temperature_centiC) * 2 seconds`

Invalid, missing, late-after-failure, or quarantined samples add nothing; a newly valid sample after a gap must not integrate the unobserved gap. Initial rebuild-time values are a freezing threshold of **0 degrees Celsius**, required normal cold exposure of **5 degrees Celsius-hours = 1,800,000 centi-degree-Celsius-seconds**, minimum fallback cold exposure of **1 degree Celsius-hour = 360,000 centi-degree-Celsius-seconds**, and a maximum elapsed-time boundary of **24 hours**. Store the exposure in `uint32_t` and saturate at 1,800,000.

Ordinary thermostat-driven warming legs do not subtract or reset accumulated exposure. Invalid/stale sensor data and any inferred disturbance recovery do not add exposure, but they also do not erase previously qualified exposure. The physical door input is not used, and no door-opening or humidity bonus is added.

Bootstrap/fallback eligibility follows this structure:

`scheduled_due = (cold_exposure >= required_exposure) OR ((elapsed_time >= maximum_elapsed_time) AND (cold_exposure >= minimum_exposure))`

Thus the elapsed-time boundary cannot trigger defrost without at least 1 degree Celsius-hour of credible frost-forming exposure. Parameter selection must not be derived from captured diagnostic logs.

The 24-hour elapsed timer advances during normal monitoring, including an inferred disturbance, while disturbance samples do not add cold exposure. At the end of every defrost attempt—successful or aborted—reset both cold exposure and the elapsed timer. After an abort and its lockout, new exposure must accumulate; the previous due condition is not retained as a pending request.

The door input remains reserved for future implementation and has no effect on monitoring, learning, cold exposure, active defrost, melt qualification, or recovery. Real door events remain environmental confounders that the algorithm must tolerate from sensor behavior alone.

Initial development keeps cold-exposure and elapsed-time progress in RAM. Production approval requires a separate decision on power-loss persistence; repeated resets must not be allowed to postpone scheduled defrost indefinitely. If EEPROM persistence is approved, it must follow the versioning, integrity, atomic-update, and wear rules in section 10.

### 11.4 Selected local-melt qualification sequence

The local-melt sequence is:

1. Capture qualified pre-defrost capacitance and context before energizing the relay.
2. Energize the relay and begin bounded compressor inhibition.
3. Apply no minimum defrost duration. Evaluate melt completion from the first valid post-actuation measurements and terminate as soon as every approved melt-success condition passes.
4. Require sensor temperature at or above the configurable minimum melt-success/lower-band temperature, initially **2 degrees Celsius**. If the capacitance model qualifies earlier, continue defrost until this condition also passes.
5. Require the guarded online-learned capacitance melt model to qualify the upward ice-to-water transition and its wet-side stabilization; do not use one universal capacitance recovery fraction, slope, plateau, or absolute value.
6. If temperature reaches the upper band edge, initially **4 degrees Celsius**, de-energize the relay to permit thermostat-controlled cooling and continue evaluating melt; this is not success or abort by itself.
7. If all success conditions pass before any abort, declare **qualified local melt success**, de-energize the relay, and create only candidate after-melt/reference-excursion data.
8. Enter recovery quarantine and verify the candidate before any trusted-model update.

The initial physical/bootstrap capacitance detector is fixed as follows:

1. Before relay energization, require five consecutive valid two-second samples and set `C_start` to their median. Record the associated timestamp and temperature. Do not begin defrost until this capture is available.
2. During defrost, convert each five consecutive valid on-schedule samples into one 10-second median capacitance block. An invalid or missed sample clears any partial block so a nominal 10-second block can never silently span an unobserved gap.
3. Initialize `C_min = C_start` and update the session minimum from valid block values. The upward-transition window is **1 minute**, comprising six 10-second blocks.
4. Across that window, calculate `positive_sum` from positive block-to-block changes and `absolute_sum` from all absolute changes. A transition window is directionally upward when its net change is positive and `positive_sum * 4 >= absolute_sum * 3`. Require this result for three consecutive 10-second evaluations.
5. After an upward transition candidate is established, track `C_peak` and `transition_rise = C_peak - C_min`. The rise must be positive.
6. Observe a **5-minute** wet-side stabilization window. Let `stabilization_range` be its maximum minus minimum block value and let `C_final` be the median of its final five blocks. Require all of: `stabilization_range * 4 <= transition_rise`; `C_final > C_start`; and `(C_final - C_min) * 4 >= transition_rise * 3`.
7. Before stabilization completes, a reversal satisfying `(C_peak - current_block) * 4 > transition_rise` cancels only the current transition candidate. Continue the same defrost session and search from the subsequently observed local minimum.
8. Preserve `C_start`, session history, and the active candidate across 2-to-4-degree relay-band transitions. Continue observing in both relay substates. The normal-operation disturbance detector is disabled during defrost because rapid temperature/capacitance changes are expected there.
9. With no trusted model, declare bootstrap local melt only when the preceding physical pattern passes, current temperature is at least 2 degrees Celsius, and no higher-priority abort exists. Terminate immediately and create one PROVISIONAL event. If it never passes, only a hard abort can end the attempt.

All ratios use cross-multiplication. No universal raw-picofarad rise, slope, or endpoint threshold is introduced. The one-minute transition, three-evaluation direction persistence, and five-minute stabilization are named rebuild-time parameters.

The configured maximum defrost time (initially four hours), 6-degree emergency ceiling, 10-minute cumulative-above-band limit, sensor fault/staleness, reset/watchdog, or internal consistency fault terminate the attempt independently and cannot produce melt success or reference candidates. The unused door input has no effect. This sequence qualifies only the sensor location; it does not prove all cabinet or evaporator ice has melted.

The capacitance side of melt success is learned online by each controller. The trusted model may contain the qualified wet endpoint/context, defrost-cycle excursion, upward-transition direction and shape descriptors, wet-side stabilization behavior, confidence, and age. Each completed event first creates a candidate; recovery and later event consistency are required before bounded commitment. The first bootstrap event necessarily uses the non-learned physical rules—temperature at or above the 2-degree parameter, a persistent upward capacitance transition, and wet-side stabilization—to create a low-confidence candidate. Until later qualified events confirm it, conservative scheduling and hard safety limits remain authoritative. Captured UART logs are never training data.

Model promotion requires three consistent qualified defrosts:

1. **NO_MODEL:** no qualified event exists. Use scheduled bootstrap and physical melt rules only.
2. **PROVISIONAL:** one qualified event exists. The model may check consistency and provide diagnostics but cannot independently qualify capacitance melt success.
3. **CONFIRMED:** two mutually consistent qualified events exist. The learned model may support melt confidence, but conservative physical qualification remains mandatory.
4. **TRUSTED:** three mutually consistent qualified events exist. The learned model may qualify the capacitance side of melt success, while the 2-degree gate and every independent safety/fallback rule remain mandatory.

Only events that finish as qualified local melt success and pass recovery/candidate verification count toward promotion. Aborted attempts create no learning event. An inconsistent candidate is rejected and must not be averaged into the model. Exact consistency bounds, restricted PROVISIONAL/CONFIRMED behavior, confidence reduction, consecutive-inconsistency invalidation, aging, and bounded-update equations remain to be specified before automatic relay control.

The same cold-exposure/schedule mechanism remains required after bootstrap as a conservative fallback. It must cover direct frost that accumulates below the capacitance detector's observable boundary. A sensor-confirmed phase event may permit an earlier defrost, but absence of capacitance evidence cannot prove absence of ice. Conversely, elapsed time without credible frost-forming exposure must not force an unnecessary defrost. Exact arbitration between event confidence and the conservative fallback remains open.

Ice detection and melt detection are separate pattern recognizers:

- **Ice pattern:** persistent frost opportunity combined with either (a) a qualified downward contextual residual/trend evaluated against the trusted defrost-cycle excursion, or (b) an upward wetting transition followed by a freezing-associated capacitance fall. Trigger at reasonable moderate coverage while the downward trend is still observable, before it reaches the low-slope full-coverage plateau. The evidence must not be explained by a disturbance. Conservative cold-exposure scheduling remains an independent fallback, and an observed thick-ice plateau is an overdue backstop rather than the normal trigger.
- **Melt pattern:** after deliberate compressor inhibition, temperature rises through the melting region and capacitance makes a persistent upward transition consistent with ice becoming liquid water. Interpret the transition both relative to defrost-start state and any qualified trusted model, and require a stable post-transition condition. Crossing 0 degrees Celsius, observing one rising sample, or returning to one capacitance value is not sufficient by itself; retained meltwater may keep the capacitance elevated.

For the initial learned ice-trend path, form a one-minute aggregate from six 10-second capacitance blocks. Maintain a 5-minute short context and a 60-minute long context, evaluated once per minute. A downward-trend flag requires the short context below the long context for 15 consecutive evaluations. With a TRUSTED excursion model, the 0.75 excursion trigger level must persist for 5 consecutive one-minute evaluations. Remember a qualified downward-trend flag for 2 hours so reaching a later low-slope plateau does not erase the preceding accumulation evidence. PROVISIONAL and CONFIRMED models cannot independently start defrost; conservative cold-exposure scheduling remains authoritative at those levels.

The inferred-disturbance detector operates only during normal monitoring. Behavior that breaks the qualified native thermostat-cycle ordering—such as unusually prolonged warming, a capacitance rise without an appropriate physical phase sequence, or a persistent capacitance discontinuity—freezes ice confidence and trusted-reference learning and pauses cold-exposure accumulation without clearing it. The 24-hour elapsed timer continues. Recovery quarantine ends only after two credible cooling/warming thermostat cycles. A capacitance offset that persists across that recovery invalidates the contextual reference and requires relearning rather than being classified as ice. Exact learned normal-cycle envelope bounds remain part of the later adaptive-model specification. During defrost this normal disturbance detector is disabled; defrost-specific sequence, relay substate, reversal, stabilization, and hard-abort rules apply instead.

Candidate pattern features to reason about from physical behavior include relative baseline movement, robust short- and long-window slopes, change-point detection, persistence, temperature/capacitance phase relationship, cycle-to-cycle shape changes, time spent in relevant thermal regions, melting-region plateau behavior, and post-defrost recovery. All calculations must be feasible with bounded memory and fixed-point integer arithmetic on the STM32C011. Selection must be justified before examining captured logs.

The frozen design's predefined verification matrix must include:

- Genuine progressive ice formation and verified complete/partial melts.
- Hot, cold, humid, and air-conditioned installation environments.
- Day/night and seasonal ambient-temperature cycles.
- Door left open and frequent short door openings.
- Hot or steaming food insertion.
- Large room-temperature food loads.
- Empty, normally loaded, and heavily loaded cabinets.
- Condensation or liquid water at the sensor.
- Sensor mounting variation, displacement, contamination, and long-term drift.
- Local ice at the sensor versus ice elsewhere.
- Native thermostat cycling patterns.
- Power interruption/reset during normal monitoring and during defrost.
- Sensor CRC errors, truncated frames, timeouts, stuck data, and implausible values.

The door input is not part of the current control algorithm. Physical door-opening scenarios remain in the environmental verification matrix because their temperature/moisture effects must be tolerated, but switch-state behavior and debounce are outside the current implementation.

## 12. Verification and fault injection

Verification must cover:

- Reset and boot GPIO waveforms at PA1 and the relay coil.
- Relay behavior during watchdog reset, brownout, debugger connection, and firmware crash.
- Sensor power-up timing and the first transmitted byte.
- Back-power current on PA4 while the sensor is off.
- UART framing/noise recovery and multiple-frame parsing.
- CRC known-answer tests and corrupted-frame tests.
- Software-UART interrupt load while receiving sensor frames.
- EEPROM power interruption at every write boundary if persistence is later added.
- Upper-temperature and maximum-time termination independent of algorithm state.
- Every environmental confounder listed in section 11.
- Cabinet-wide and time-temperature safety measurements using independent instruments.

## 13. Open decisions and required evidence

1. Confirm valid numerical ranges and explicit invalid/sentinel sensor values.
2. Validate the selected two-second initial cadence in normal monitoring and active defrost.
3. Validate or revise the split acquisition policy: three attempts in normal monitoring, one attempt per active-defrost cycle, 100 ms powered-off intervals, and an initial configurable 10-cycle active-defrost failure limit.
4. Specify learned normal-cycle envelope bounds and the later-event consistency, candidate-commit, aging, and bounded-update equations independently of captured UART logs.
6. Validate or replace the configurable lower defrost-band/melt-success temperature, initially 2 degrees Celsius.
7. Validate or replace the 4-degree upper defrost-band threshold, 6-degree emergency ceiling, 10-minute cumulative-above-band limit, and maximum-defrost-duration parameter initially four hours using distributed temperature measurements.
8. Decide whether a second representative cabinet temperature sensor is required.
9. Determine whether cold-exposure, elapsed-time, trusted-reference, and candidate state require EEPROM persistence for production.
10. Revisit reset ride-through only if interrupted defrosts prove operationally significant.

## 14. Planned phases

1. Close hardware and protocol uncertainties.
2. Implement and verify safe startup, relay control, watchdog handling, timing, and sensor acquisition without an automatic defrost algorithm.
3. Build measurement-only firmware for interface diagnostics and human visibility; do not use its captured logs to create or tune the algorithm.
4. Specify the deterministic, self-referenced ice/melt algorithm and all parameters from stated physical reasoning, then version-freeze it.
5. Write acceptance criteria first, then verify the frozen design against the scenario matrix without mining logs for changes.
6. Integrate the bounded defrost state machine and compile-time experimental parameters.
7. Perform fault injection, power/reset testing, long-duration testing, and mains-load validation.
8. Approve production constants and persistence only from recorded evidence.

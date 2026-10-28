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
| PA11 [PA9] | DOOR_SENSOR | Door input; bracketed name is schematic notation |
| PA12 [PA10] | Not configured | Unused; formerly a test point |
| PA13 | DEBUG_SWDIO | SWD debug |
| PA14 | DEBUG_SWCLK / BOOT0 | SWD clock and BOOT0 sharing |

## 3. Safety invariants

1. Reset, loss of MCU power, unconfigured GPIO, firmware failure, and watchdog recovery must default PA1 to the state that de-energizes the relay and returns compressor control to the refrigerator.
2. Normal cooling must never depend on successful sensor communication, EEPROM access, logging, or completion of the defrost algorithm.
3. Logging must never delay or change relay safety handling.
4. Invalid, missing, implausible, or stale sensor data must not extend defrost. A sensor fault during defrost de-energizes the relay and aborts the attempt.
5. The relay must always be de-energized after the maximum defrost time or upper-temperature ceiling is reached, regardless of capacitance.
6. A defrost decision must be persistent and debounced. A single measurement cannot energize the relay.
7. Rapid relay cycling must be prevented by configurable state-duration and lockout parameters.
8. EEPROM corruption or loss must fall back to conservative compiled defaults and normal refrigerator control.


## 4. Serial interfaces and timing

USART2 receives sensor data on PA4 at 9600 baud. PA2 logs independently through a transmit-only software UART at 115200 baud.

The production log transmitter must use timer-based timing and a bounded buffer. Sensor reception, watchdog service, timekeeping, and relay safety have priority. Logs may be paused or dropped under load; safety work may not be delayed to preserve logs. Timing and interrupt-load measurements are required before enabling verbose logs in defrost tests.

The measurement-only firmware temporarily uses a blocking, SysTick-cycle-timed PA2 transmitter after sensor reception is complete and sensor power is off. It disables interrupts for approximately one 8N1 character at a time. This is acceptable only while the relay is permanently de-energized and must be replaced or revalidated before automatic defrost control is introduced. Verify its 115200-baud waveform and accumulated timing error with test equipment.

## 5. Sensor protocol and acquisition

Powering the sensor through `CAP_SENSOR_EN` starts its transmission. It begins streaming frames approximately 50 ms after power-up and may transmit multiple frames while powered.

### 5.1 Frame

The frame is eight bytes:

| Byte | Meaning |
|---|---|
| 0 | Start marker `0xAA` |
| 1-2 | Processed signed temperature, big-endian `int16_t` |
| 3-4 | Processed capacitance, big-endian `uint16_t` picofarads |
| 5-6 | Received CRC, most-significant byte first |
| 7 | End marker `0xAC` |

There is no application-side scaling. The sensor reports processed degrees Celsius and picofarads. Every multi-byte field uses big-endian byte order.

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

The exact state machine will be designed after requirements review, but it must separate at least these responsibilities:

1. **Boot self-check:** establish safe outputs first, determine reset cause, validate configuration, and leave the relay de-energized.
2. **Normal monitoring:** permit refrigerator operation and collect periodic sensor measurements.
3. **Ice-candidate confirmation:** require sustained multi-sample evidence and reject known disturbances.
4. **Defrost active:** energize the relay, monitor melt evidence and all hard limits.
5. **Defrost completion/recovery:** de-energize the relay, observe the post-defrost response, discard or reset event-local comparison state as specified, and prevent immediate retriggering.
6. **Fault/disabled automatic defrost:** keep the relay de-energized while reporting diagnostics.

## 7. Defrost control policy

### 7.1 Initial policy choice

The first design will use **melt evidence with hard termination ceilings**, not regulate the sensor around 4 degrees Celsius.

Capacitance and temperature history will provide evidence that ice has melted. Temperature alone will not declare complete melting. Passing 0 degrees Celsius at the single sensor proves only that the sensor location is above freezing; ice may remain elsewhere because of thermal gradients, placement, water/ice mixtures, and differing heat paths.

Defrost terminates immediately when any of the following occurs:

- The future validated algorithm reports sufficient melt completion evidence.
- Sensor temperature reaches the configured upper-temperature ceiling.
- Total defrost time reaches the configured maximum.
- Sensor data becomes unavailable, invalid, implausible, or stale beyond its allowed limit.
- A reset or other safety fault occurs.

After termination, the relay is de-energized. The controller does not attempt to hold the sensor near the upper-temperature ceiling in the initial design.

### 7.2 Provisional experimental limits

- Upper sensor-temperature ceiling: **4 degrees Celsius, provisional**.
- Maximum total defrost-stage duration: **6 hours, provisional**.

Both are rebuilt into firmware during experiments and must remain named parameters. Neither value is approved for production until cabinet-wide temperature and food-safety testing is complete.

### 7.3 Single-sensor limitation

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
- Minimum relay-energized/defrost duration, if testing establishes one.
- Maximum total defrost duration.
- Upper defrost sensor-temperature ceiling.
- Minimum normal-cooling-permitted interval between defrost attempts.
- Post-defrost observation/recovery interval.
- Lockout after an aborted or failed defrost.
- Minimum relay-state duration to prevent chatter.
- Sensor receive timeout.
- Sensor power-cycle retry delay and maximum retry count.
- Maximum consecutive sensor failures before automatic defrost is disabled.
- Algorithm filter windows, confidence thresholds, hysteresis, and persistence counts.

Parameter values will initially be changed by rebuilding firmware. A serial command interface and EEPROM-editable configuration are not currently required.

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
- Freeze learning during suspected ice, defrost, sensor faults, door/hot-load-like disturbances, and unstable recovery.
- Bound update magnitude and rate, retain confidence/age metadata, and invalidate or re-bootstrap models after discontinuities that make them untrustworthy.
- Short-lived comparisons within the current cooling, warming, or deliberate-defrost event may be used and reset when the event becomes invalid.
- Prefer dimensionless direction, ordering, persistence, and bounded within-event change over installation-specific absolute capacitance.
- Condition capacitance behavior on temperature history rather than treat capacitance independently.
- Use direction, rate, persistence, sequence, and physically justified transition behavior.
- Accumulate confidence from multiple agreeing features; no single sample or feature may confirm ice or melt completion.
- Use separate entry and exit confidence with hysteresis so the state cannot chatter.
- Reset or invalidate event-local comparison state during sensor faults, door/hot-load-like disturbances, resets, and other broken observation sequences; preserve a persistent trusted reference only if its version, integrity, age, and confidence checks pass.
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

The configured fraction is an operational trigger point, not a physical ice-coverage percentage. A reference excursion below its minimum valid magnitude is ineligible; capacitance-based triggering is then unavailable and the conservative cold-exposure fallback remains authoritative. The wet-to-initial-freeze swing may still contribute optional freezing/recovery event evidence, but it is not stored or required as the accumulation denominator.

Reference updates use two stages. A newly observed after-melt value and excursion are **candidates** only. A candidate may be committed to the trusted model only after the defrost ends as qualified local melt success, recovery quarantine completes, subsequent thermostat cycles are credible, integrity/context checks pass, and bounded-update rules accept it. Temperature-ceiling, maximum-time, sensor-fault, reset, or otherwise uncertain defrost outcomes cannot update the trusted reference. The first bootstrap candidate has limited confidence and the conservative schedule remains authoritative until later qualification confirms the model.

The supplied sensor response gives physically meaningful endpoint and phase-transition directions. From a dry condition, full ice coverage has a higher endpoint; liquid water has a still-higher endpoint, although dry is not expected in service. A water-arrival/wetting event moves capacitance upward; freezing and subsequent ice accumulation move it downward toward the thick-ice plateau; melting moves it upward again. The algorithm should trigger from a qualified persistent downward contextual deviation and trend while slope remains meaningfully negative, before it approaches the low-slope plateau. These transitions may contribute to confidence only when their temperature ordering, frost opportunity, persistence, context, and disturbance checks also agree. A high value alone cannot distinguish water from ice, and a downward transition alone cannot distinguish freezing, gradual accumulation, drainage, removal, contamination shift, or sensor drift. Negligible sensor noise removes the need for a noise-derived threshold but does not remove data-validity checks, persistence, hysteresis, or arithmetic denominator guards.

### 11.2 Operating context, recovery, and bootstrap

The refrigerator does not maintain one stable temperature. Its thermostat creates recurring cooling and warming legs, producing an approximately sawtooth temperature trajectory. Therefore, **normal operation** means credible recurring thermostat-driven cycles, not a flat temperature. The contextual model must at least distinguish temperature region and thermal direction; the same temperature on a cooling leg and a warming leg may legitimately have different capacitance behavior.

**Recovery** is a quarantined transient after deliberate defrost or a major disturbance. After defrost terminates and compressor power is made available again, temperature returns from the warmed condition toward recurring thermostat cycling; liquid water may drain, redistribute, or refreeze, and capacitance may still be settling. A similar recovery interval follows door/hot-load-like disturbances. Recovery is not a clean reference by definition. Trusted-reference learning remains disabled until predefined qualification rules show valid sensor data, absence of known disturbance, and a sufficient number of credible alternating cooling/warming legs. The required cycle count and consistency bounds remain open parameters.

The selected startup approach is **conservative scheduled bootstrap**:

1. Start with no trusted wet, low-ice reference model and keep the relay de-energized so normal refrigerator control remains available.
2. Monitor valid temperature/capacitance and accumulate a conservative cold-exposure condition while also enforcing a maximum elapsed-time boundary; do not make a learned ice decision yet. The combination must be specified so elapsed time alone cannot force an unjustified warming event when credible frost-forming exposure has not occurred.
3. When the bootstrap condition is satisfied, capture the qualified pre-defrost capacitance and perform one bounded defrost using independent time, temperature, sensor-fault, and relay-protection limits.
4. If local melt success is qualified, capture candidate after-melt wet capacitance and candidate defrost-cycle excursion; an abort cannot produce reference candidates.
5. Treat the subsequent interval as recovery quarantine; do not immediately declare its data clean.
6. Verify the candidate against qualified recurring cooling/warming cycles after recovery.
7. Commit the candidate only after its predefined confidence, consistency, and bounded-update requirements pass. If bootstrap defrost aborts or recovery never qualifies, retain no trusted reference and enter the specified conservative fallback behavior.

Bootstrap timing, cold-exposure calculation, interaction with the maximum elapsed-time boundary, required recovery cycles, candidate-commit rules, and fallback behavior remain unresolved and must be defined before implementation.

The same cold-exposure/schedule mechanism remains required after bootstrap as a conservative fallback. It must cover direct frost that accumulates below the capacitance detector's observable boundary. A sensor-confirmed phase event may permit an earlier defrost, but absence of capacitance evidence cannot prove absence of ice. Conversely, elapsed time without credible frost-forming exposure must not force an unnecessary defrost. Exact arbitration between event confidence and the conservative fallback remains open.

Ice detection and melt detection are separate pattern recognizers:

- **Ice pattern:** persistent frost opportunity combined with either (a) a qualified downward contextual residual/trend, optionally expressed by an approved wet-to-initial-freeze phase-scaled accumulation index, or (b) an upward wetting transition followed by a freezing-associated capacitance fall. Trigger at reasonable moderate coverage while the downward trend is still observable, before it reaches the low-slope full-coverage plateau. The evidence must not be explained by a disturbance. Conservative cold-exposure scheduling remains an independent fallback, and an observed thick-ice plateau is an overdue backstop rather than the normal trigger.
- **Melt pattern:** after deliberate compressor inhibition, temperature rises through the melting region and capacitance makes a persistent upward transition consistent with ice becoming liquid water. Interpret the transition both relative to defrost-start state and any qualified trusted model, and require a stable post-transition condition. Crossing 0 degrees Celsius, observing one rising sample, or returning to one capacitance value is not sufficient by itself; retained meltwater may keep the capacitance elevated.

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

Although the door input is not part of the first control algorithm, door events must be deliberately included in the predefined verification matrix. If the frozen algorithm cannot meet the prewritten acceptance criteria without a direct door signal, report that limitation and reconsider the input rather than tuning from captured traces.

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
2. Select the eventual production cadence in normal monitoring and active defrost; initial measurement logging uses 2 seconds.
3. Validate or revise the initial acquisition policy of three attempts with a 100 ms powered-off retry interval.
4. Define the consecutive-failure policy.
5. Specify and version-freeze the deterministic self-referenced temporal-pattern ice/melt algorithm independently of captured UART logs.
6. Determine whether an early successful melt may terminate below 0 degrees Celsius at the sensor, or whether a temperature-transition condition is also required.
7. Validate or replace the provisional 4-degree ceiling and 6-hour maximum using distributed temperature measurements.
8. Decide whether a second representative cabinet temperature sensor is required.
9. Confirm/add the relay MOSFET gate pull-down and complete the mains/relay safety review.
10. Decide whether the door input must enter the production algorithm.
11. Determine whether any EEPROM persistence is genuinely necessary.
12. Revisit hardware reset ride-through only if interrupted defrosts prove operationally significant.

## 14. Planned phases

1. Close hardware and protocol uncertainties.
2. Implement and verify safe startup, relay control, watchdog handling, timing, and sensor acquisition without an automatic defrost algorithm.
3. Build measurement-only firmware for interface diagnostics and human visibility; do not use its captured logs to create or tune the algorithm.
4. Specify the deterministic, self-referenced ice/melt algorithm and all parameters from stated physical reasoning, then version-freeze it.
5. Write acceptance criteria first, then verify the frozen design against the scenario matrix without mining logs for changes.
6. Integrate the bounded defrost state machine and compile-time experimental parameters.
7. Perform fault injection, power/reset testing, long-duration testing, and mains-load validation.
8. Approve production constants and persistence only from recorded evidence.

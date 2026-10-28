# Defrost Controller Task Tracker

Last updated: 2026-10-28

This file tracks decisions, investigations, implementation, and verification. Maintain it whenever the plan or project status changes.

Checkbox meaning:

- `[x]` means the design decision or documentation task is complete. It does not imply firmware or hardware has been implemented.
- `[ ]` means unresolved, not started, or incomplete.
- A task may be checked only when its stated completion evidence exists.

## A. Requirements and decisions

- [x] **A01 — Establish clean-slate rule.** No assumptions, constants, code, or algorithms may be imported from previous projects.
- [x] **A02 — Define system boundary.** Controller interrupts only compressor power; refrigerator thermostat/control remains responsible for normal cooling.
- [x] **A03 — Define relay terminology and fail-safe direction.** Energized means compressor inhibited/defrost; de-energized means compressor power available/normal control.
- [x] **A04 — Record hardware and pin baseline.** Pin map, EEPROM, sensor, clock choice, relay, logs, and debug pins are documented.
- [x] **A05 — Separate serial interfaces.** Sensor uses USART2/PA4 at 9600 baud; PA2 software-UART logs at 115200 baud with lower priority.
- [x] **A06 — Define sensor frame and CRC coverage.** Eight-byte framed response and CRC-16/CCITT-FALSE coverage are documented.
- [x] **A07 — Choose power-triggered sensor acquisition concept.** Arm reception before PA6 power-on, accept a valid streamed frame, power off after 10 ms, and retry by bounded power cycling.
- [x] **A08 — Choose initial defrost termination policy.** Use melt evidence plus independent hard ceilings; do not regulate around 4 degrees Celsius initially.
- [x] **A09 — Record provisional experimental ceilings.** Local sensor ceiling 4 degrees Celsius and maximum defrost time 6 hours; neither is production-approved.
- [x] **A10 — Choose initial reset/power-loss policy.** Abandon defrost, de-energize relay, and return control to the refrigerator; do not auto-resume.
- [x] **A11 — Choose algorithm principle.** Use self-referenced temporal/state-transition patterns and confidence, not installation-specific raw capacitance values.
- [x] **A12 — Record single-sensor limitation.** Local temperature cannot certify the warmest cabinet or food temperature.
- [x] **A13 — Define CubeIDE file-ownership rule.** Use `.ioc` for generated hardware configuration, Cube `USER CODE` regions for small integrations, and separate owned modules for application logic.
- [x] **A14 — Select and configure the internal system clock.** HSI48 divided by 4 supplies a 12 MHz system/peripheral clock; HSE pins are not configured.
- [x] **A15 — Reject reliance on one conditioned installation reference.** The production algorithm must not require customer commissioning or assume that a single learned site profile remains valid; uncontrolled placement and environment can invalidate it.
- [x] **A16 — Permit guarded contextual online learning.** Learning may adapt to current operating conditions, but must separate short-term context from a protected trusted wet, low-ice reference and must not use captured UART logs for training or tuning.
- [x] **A17 — Select conservative scheduled bootstrap.** Start without a trusted model, wait for a predefined conservative elapsed-time/cold-exposure condition, perform a safety-bounded defrost, quarantine recovery, and form a candidate reference only from subsequently qualified thermostat cycles.
- [x] **A18 — Define normal temperature behavior conceptually.** Normal refrigerator operation is recurring thermostat-driven cooling/warming legs with a sawtooth-like temperature trajectory, not a fixed stable temperature.
- [x] **A19 — Select contextual-reference architecture.** Compare capacitance with a guarded wet, low-ice reference for the applicable temperature-region and thermal-direction context; do not decide from raw capacitance and do not normalize by sensor-noise variation.
- [x] **A20 — Select combined bootstrap trigger concept.** Use credible cold exposure together with a maximum elapsed-time boundary; elapsed time alone must not force an unjustified warming event without frost-forming exposure.
- [x] **A21 — Record sensor transition directions.** Treat liquid-water capacitance rise, freezing-associated fall, and melting-associated rise as sensor-design inputs; temperature-only capacitance change and thermal lag are negligible, while contamination or a retained water film may permanently shift the baseline.
- [x] **A22 — Record sensor material-state ordering.** At comparable conditions, capacitance endpoints are ordered dry < fully ice-covered < liquid water; absolute values and separations remain installation-dependent.
- [x] **A23 — Define the ice-growth response and target.** Capacitance falls as ice accumulates and approaches a lower plateau when thick ice fully covers the sensor. Trigger from the earlier persistent contextual downward trend; the plateau is an overdue backstop, not the normal target. Do not infer numerical ice thickness.
- [x] **A24 — Require a hybrid defrost trigger.** Permit earlier triggering from qualified phase/event confidence, but retain conservative cold-exposure scheduling as an independent fallback when qualified capacitance evidence is absent, interrupted, or insufficient.
- [x] **A25 — Define the installed reference state.** The sensor is expected never to be dry; use a wet, low-ice post-defrost reference. Thick ice is the lowest contextual in-service condition, but not a universal raw threshold or quantitative thickness measurement.
- [x] **A26 — Remove sensor noise as an algorithm dependency.** Sensor noise is sufficiently low not to affect the capacitance trend; retain validity, persistence, hysteresis, and defensive normalization checks, but do not derive thresholds from noise.
- [x] **A27 — Select qualified defrost-cycle excursion scaling.** Use the previous qualified pre-defrost-to-after-melt capacitance excursion as the primary adaptive magnitude scale, compare by fixed-point cross-multiplication, and retain wet-to-initial-freeze swing only as optional event evidence.
- [x] **A28 — Require candidate/trusted reference separation.** Aborted or uncertain defrosts cannot update the trusted model; after-melt and excursion candidates require qualified local melt, recovery quarantine, credible thermostat cycles, integrity/context checks, and bounded commit rules.

## B. Blocking hardware and protocol questions

- [x] **B01 — Confirm temperature and capacitance byte order.** All multi-byte sensor fields are big-endian; record a real-frame decoding example during board testing.
- [ ] **B02 — Complete the sensor numerical contract.** Define valid ranges, saturation behavior, and sentinel/invalid outputs; sensor noise does not require further algorithm characterization.
- [ ] **B03 — Verify PA4 unpowered behavior.** Measure possible back-power current and select the safe GPIO state while PA6 is LOW.
- [ ] **B04 — Verify relay gate pull-down.** Confirm whether a gate-to-ground resistor exists; add one if absent and validate reset waveforms.
- [ ] **B05 — Complete relay and mains review.** Verify compressor inrush/motor rating, contact mapping, suppression, fuse, clearance, creepage, and failure modes.
- [ ] **B06 — Measure sensor startup/stream timing.** Capture power enable, first byte timing, frame repetition, and shutdown behavior with test equipment.
- [ ] **B07 — Validate generated clock configuration in CubeIDE.** Open the `.ioc`, confirm HSI48 divided by 4 and 12 MHz SYSCLK/HCLK/PCLK, regenerate, and confirm no unintended configuration changes.
- [x] **B08 — Classify ice-coverage response.** Accumulating ice produces a detectable downward contextual trend and full thick coverage produces a lower plateau. Use the early trend for control and the plateau only as an overdue backstop; do not treat capacitance as a calibrated thickness measurement.

## C. Parameters still requiring discussion

- [x] **C01 — Select initial measurement sample cadence.** Schedule an acquisition every 2 seconds and skip missed slots after overrunning retries. Production cadence remains a later decision.
- [ ] **C02 — Select active-defrost sample cadence.** Must detect melt transitions and safety limits promptly.
- [ ] **C03 — Validate acquisition retry policy.** Initial firmware uses three attempts and 100 ms sensor-off interval; consecutive-failure behavior and final production values remain open.
- [ ] **C04 — Select relay protection timings.** Minimum state duration, normal-operation interval between attempts, recovery interval, and failed-attempt lockout.
- [ ] **C05 — Define temperature/time safety validation criteria.** Determine how provisional 4-degree and 6-hour limits will be accepted, reduced, or rejected.
- [ ] **C06 — Decide whether the door input enters production control.** Initial experiments may log door events manually, but resilience may require the direct input.
- [ ] **C07 — Decide whether another cabinet temperature sensor is required.** Required if the controller must guarantee the warmest cabinet location.
- [ ] **C08 — Decide justified EEPROM contents.** Do not store parameters or adaptive state until recovery, integrity, wear, and failure behavior are defined.

## D. Pattern-algorithm design

- [ ] **D01 — Define signal-quality layer.** Validity, freshness, plausibility, CRC/UART error handling, and missing-data behavior.
- [ ] **D02 — Choose trend estimators.** Define bounded short/long fixed-point temperature and capacitance slopes for cycle classification, moderate-ice trend detection, and plateau backstop; do not add noise-driven filtering without another stated need.
- [ ] **D03 — Define dual-timescale representation.** Specify temperature bins, thermal-direction states, short-term context state, trusted/candidate wet references and defrost-cycle excursions, confidence and age, signed residual, cross-multiplied trigger fraction, invalid-excursion fallback, storage, and memory limits.
- [ ] **D04 — Define disturbance detector.** Door-like warming, hot/steaming load, condensation, installation/ambient drift, and recovery holdoff patterns.
- [ ] **D05 — Define hybrid ice-trigger arbitration.** Specify qualified phase/event confidence, accumulation/decay, persistence, hysteresis, and how it combines with the independent cold-exposure fallback without treating absent capacitance evidence as proof of no ice.
- [ ] **D06 — Define melt-confidence pattern.** Defrost-start reference, transition sequence, wet-versus-frozen response, stability, hysteresis, and success rules.
- [ ] **D07 — Define unsuccessful-defrost outcomes.** Distinguish verified success, upper-temperature abort, maximum-time abort, sensor-fault abort, and reset abort.
- [ ] **D08 — Define learning guards and invalidation.** Specify bootstrap, pre-defrost capture, qualified local-melt candidate creation, recovery verification, candidate commit/rejection, freeze conditions, bounded update rate, confidence/age decay, transient-state reset, persistent-reference invalidation, and recovery after installation/environment changes.
- [ ] **D09 — Define algorithm resource budget.** Flash, SRAM, stack, execution time, and worst-case interrupt latency.
- [ ] **D10 — Specify scheduled-bootstrap and recovery qualification.** Define the bootstrap schedule/cold-exposure condition, bounded-defrost outcome requirements, recovery quarantine, required credible cooling/warming legs, candidate-reference commit criteria, and no-reference fallback.

## E. Measurement and experimental work

- [x] **E01 — Establish the diagnostic-log boundary.** UART captures are for interface/debug visibility only and cannot be used to derive, tune, train, or justify the algorithm.
- [ ] **E02 — Complete measurement-only firmware.** Sensor acquisition and CSV measurement/error logging are implemented and pass ARM compiler syntax checks. Board validation, reset-cause logging, and event annotations remain.
- [ ] **E03 — Verify sensor-interface repeatability.** Use captures only to confirm framing, scaling, timing, and communication stability; do not infer algorithm behavior.
- [ ] **E04 — Version-freeze the algorithm before automatic-control tests.** Record its physical reasoning, equations, state transitions, parameters, and safety bounds without using captured logs.
- [ ] **E05 — Predefine scenario acceptance criteria.** Cover missed ice, unnecessary defrost, unsafe warming, nuisance faults, and unsuccessful defrost before running scenarios.
- [ ] **E06 — Execute the frozen clean/ice/melt scenarios.** Judge only against prewritten pass/fail criteria; do not mine traces to tune the design.
- [ ] **E07 — Execute the frozen disturbance scenarios.** Door events, hot/steaming food, condensation/water, load changes, and sensor movement.
- [ ] **E08 — Execute the frozen environmental scenarios.** Ambient temperature, humidity, installation location, day/night, and seasonal conditions.
- [ ] **E09 — Verify cabinet-wide safety independently.** Use calibrated reference instruments for predefined safety acceptance only, not algorithm derivation.
- [ ] **E10 — Report failures without trace-driven retuning.** Any redesign or changed parameter method requires an explicit user decision.

## F. Firmware implementation

- [ ] **F01 — Implement safe early startup and GPIO states.** Relay must remain de-energized through initialization and all failure paths.
- [ ] **F02 — Implement watchdog and reset-cause handling.** Reset aborts defrost and returns normal control.
- [ ] **F03 — Complete USART2 sensor receiver and CRC verification.** Big-endian decoding, stream resynchronization, timeout, retry, and multiple-frame handling are implemented and compile cleanly; known-answer/unit tests and board verification remain.
- [ ] **F04 — Complete PA2 software-UART logging.** Experimental blocking 115200-baud output is implemented for measurement-only operation after sensor power-off. Waveform validation and the production timer-driven bounded implementation remain.
- [ ] **F05 — Implement monotonic timekeeping and named compile-time parameters.** Handle timer wraparound safely.
- [ ] **F06 — Implement explicit controller state machine.** Boot, monitoring, disturbance, suspicion, confirmed ice, defrost, melt verification, recovery, and fault states.
- [ ] **F07 — Implement the version-frozen pattern detector.** Only after its specification and prewritten acceptance criteria are complete; do not derive it from captured logs.
- [ ] **F08 — Implement EEPROM records only if approved.** Versioning, CRC, atomic recovery, and wear tests required.

## G. Verification and release evidence

- [ ] **G01 — Unit-test frame parsing and CRC with known-answer and corrupted frames.**
- [ ] **G02 — Test sensor timeout, retry, stuck stream, noise, partial frame, and implausible data.**
- [ ] **G03 — Measure software-UART interference and worst-case interrupt latency.**
- [ ] **G04 — Test relay behavior during boot, watchdog reset, brownout, debugger activity, and firmware hangs.**
- [ ] **G05 — Inject every defrost termination path.** Melt success, 4-degree ceiling, 6-hour ceiling, sensor fault, reset, and internal fault.
- [ ] **G06 — Run environmental/confounder verification.** Use the frozen design and acceptance criteria written before testing; do not tune from captured logs.
- [ ] **G07 — Validate cabinet-wide food-temperature safety.** Production limits cannot be approved without this evidence.
- [ ] **G08 — Run long-duration endurance testing.** Include relay cycles, sensor power cycles, resets, drift, and any EEPROM writes.
- [ ] **G09 — Review production constants and residual risks.** Record evidence and explicit approval for every safety-related parameter.

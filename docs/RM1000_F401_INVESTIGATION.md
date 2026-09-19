# Biltema RM1000 / STM32F401 investigation

This is the living handoff for the software-baseline phase. It deliberately
separates code and build evidence from physical mower evidence.

## Baseline

| Item | Value |
| --- | --- |
| Upstream | <https://github.com/mowglinext/mowglinext> |
| Fork | <https://github.com/liltaket/mowglinext-f401-rm1000> |
| Upstream branch | `dev` |
| Upstream commit | `e36ecea9c17a107cb85dc7647139025f4775ca1e` |
| Baseline date | 2026-09-19 |
| Integration branch | `test/rm1000` |
| Closest current target | `Yardforce500B` / STM32F401VC |

The fork was renamed from the account's existing MowgliNext fork because
GitHub permits only one fork per account in the same repository network.
Existing fork branches were preserved. `origin` points to the fork and
`upstream` points to the repository above.

## Hardware context

- Mower under investigation: Biltema RM1000.
- The closest known MowgliNext profile is YardForce 500B / STM32F401VC.
- Custom firmware has reportedly been used previously.
- No RM1000-specific pinout, motor protocol, frame length, command-byte or
  ADC-temperature wiring difference is treated as established yet.
- This task performs no flashing or physical mower test.

## Findings

### RTC and backup registers

Status: **FIXED / BUILD VERIFIED**

`firmware/stm32/ros_usbnode/src/adc.c` defined a zeroed `hrtc`. F401 HAL backup
access dereferences `hrtc.Instance`, while F103 happened not to. Clean fix
commit `3b0e9781e46c95fa033f45cba8278af6cb55bac3` initializes the handle before
the first access, opens backup-domain writes before enabling the RTC clock,
keeps F103's separate BKP clock, and closes access after restore.

DR6 now carries a 16-bit-compatible format marker; DR5 remains the watchdog
breadcrumb. Missing, non-finite, or code-range-invalid charge persistence is
reset and repaired before charger control. The accepted ranges come from the
existing 0..2.8 Ah SOC model and current ADC conversion, not RM1000 assumptions.
Yardforce500B and Yardforce500 builds plus guards passed. Hardware validation:
**false**.

### Motor feedback ownership and validation

Status: **FIXED / BUILD VERIFIED / INDEPENDENTLY CODE REVIEWED**

Affected files include `src/drivemotor.c`, `src/blademotor.c`, their headers,
`src/main.c`, and the motor-control path in
`src/ros/ros_custom/cpp_main.cpp`.

Confirmed problems on the baseline:

- application logic reads error bytes directly from DMA receive storage;
- validated frames are not handed off as immutable stable snapshots;
- drive foreground code can race a newly armed receive and can overwrite a
  newer ISR result when clearing the old flag;
- no completed-valid-feedback freshness requirement gates all non-zero drive
  and blade paths;
- drive collision auto-reverse does not apply the same motor-feedback gate;
- UART/DMA start results are ignored and initialization advances even when TX
  did not start;
- request buffers can be modified while DMA owns them;
- UART faults are not recorded or dispatched to controlled foreground
  recovery, and incomplete fixed-length receives have no bounded resync;
- a low-level motor inhibit alone would leave wheel/yaw PI state winding up,
  so the link fault must also participate in the main `hard_stop` decision.

Clean fix commit `17325ce97d17823b8ab7c15ccea2d5c26128b9dc`
implements raw DMA storage -> completion validation -> immutable snapshot with
sequence/timestamp -> foreground consumption.
Invalid or partial input must not refresh the age of the last valid sample.
Never-seen, stale, controller-error and recovery states inhibit non-zero output
while preserving the ability to transmit OFF/zero and poll for resynchronization.
The shared drive/blade inhibit is sticky across transient faults. Re-arm requires
both links to be healthy plus a newer, validated host command that establishes
current combined zero drive and blade-off intent; cached pre-fault intent cannot
resume motion. All drive states, including collision reverse, and the final
DMA handoff obey the inhibit. Main-loop hard stop resets wheel/yaw controller
state. RX-only recovery leaves a normal TX alone until its bounded deadline,
then separately aborts a stuck TX.

The software freshness limits are 75 ms for drive feedback and 350 ms for blade
feedback. They are not exact physical stop deadlines: polling cadence, UART
transfer/controller response, controller action/watchdog and mechanical motion
add latency. The exact zero/OFF wire timing and physical response require HIL
measurement. Yardforce500B and Yardforce500 builds plus guards passed.

Open upstream PR [#559](https://github.com/mowglinext/mowglinext/pull/559)
overlaps the blade snapshot/DMA ownership area, but also adds blade reversal.
Its own handoff keeps coast-down safety hardware-pending. This baseline will
not import the reversal feature or treat reported zero RPM as proof that the
physical rotor has stopped.

Hardware validation: **false**.

### F401 panel UART RX GPIO

Status: **FIXED / BUILD VERIFIED**

The F401 path selected USART1 AF7 for panel RX while leaving the pin in plain
input mode. Commit `942595ac79d3d7dfa16767cb36ea07a3c56b7712` selects
alternate-function push-pull mode only for the F401 path; STM32F103 remains a
plain input as required by its GPIO peripheral. Yardforce500B and Yardforce500
builds plus firmware guards passed. Hardware validation: **false**.

### ADC and blade-temperature input

Status: **NEEDS HARDWARE VERIFICATION**

`ADC_Charging_Init()` configured GPIOC without explicitly enabling the GPIOC
clock in that initialization path. Commit
`62a7bd44af5f549813c70a8503d40be7e1012273` enables it before GPIO
configuration. Yardforce500B and Yardforce500 builds plus firmware guards
passed. Hardware validation: **false**.

The same code configures PC2 as analog but samples ADC channel 13. STM32F401
device definitions identify those as distinct external ADC channels; the code
comment and selected channel therefore disagree. Repository evidence does not
establish whether the mower PCB routes the blade NTC to PC2/channel 12 or
PC3/channel 13. Do not change the channel or pin until schematic/continuity or
live ADC evidence establishes the physical net.

### SWO and USB

Status: **DOCUMENTED — NO SPECULATIVE CHANGE**

The committed 500B board selection sets `DEBUG_TYPE_SWO`, so `debug_printf()`
writes through ITM/SWO on F401. The repository also supplies an OpenOCD SWO
viewer target. This remains an A/B observation point for real hardware.

`USB_DEVICE_Init()` drives PA12 (USB D+) low as a GPIO before starting the USB
device stack only when `BOARD_YARDFORCE500_VARIANT_B` is false. The current
F401/500B path deliberately skips that explicit D+ reconnect pulse and starts
the USB device stack directly. No objective RM1000 USB defect has been
established, so behavior is left unchanged for the software baseline and the
difference remains a hardware A/B-test candidate.

## Changes

| Area | Finding | Fix | Commit | Build tested | Hardware tested |
| --- | --- | --- | --- | --- | --- |
| F401 RTC | Uninitialized handle and unchecked backup payload | Initialize/order/close backup access; mark and range-check charge state | `3b0e9781` | F401 + F103 pass | No |
| Motor RX/UART | Raw DMA consumption, no freshness gate, weak recovery | Validated snapshots, age/error gates, shared sticky inhibit, explicit zero re-arm, bounded RX/TX recovery | `17325ce9` | F401 + F103 pass | No |
| F401 panel RX | AF7 selected while pin remains GPIO input | F401 RX uses AF push-pull; F103 unchanged | `942595ac` | F401 + F103 pass | No |
| ADC GPIOC clock | Clock not enabled in ADC init path | Enable GPIOC before analog-pin configuration | `62a7bd44` | F401 + F103 pass | No |
| ADC NTC pin/channel | PC2 comment conflicts with channel 13 | No speculative change | n/a | n/a | No |
| SWO / USB reconnect | Current behavior inspected | No change | n/a | n/a | No |

## Hardware test plan

Use the final `test/rm1000` commit and record its exact SHA plus the generated
binary/ELF hashes. Cutting blades must be removed, normal STOP/lift/tilt wiring
must remain active, the mower must be secured against wheel motion, and ST-Link
recovery must be available.

1. Build the documented Yardforce500B environment and retain build logs and
   artifact hashes.
2. Cold-start and warm-reset repeatedly. Record reset cause/watchdog breadcrumb;
   reject unexpected reset loops.
3. Confirm USB enumeration, disconnect/reconnect behavior, stable host link and
   telemetry without changing SWO/USB behavior first.
4. With non-zero drive/blade requests inhibited, verify battery, charge and
   status telemetry, panel buttons/LED communication, and plausible ADC values.
5. Capture raw drive and blade UART RX frames before proposing any RM1000
   protocol change. Record frame lengths, headers, checksums, cadence and error
   bytes.
6. Verify that no motor output is authorized before a fresh, complete, valid,
   error-free controller response exists.
7. Inject or reproduce missing, incomplete, malformed, controller-error and
   stale responses, plus stuck TX. Measure the actual zero/OFF packet deadline
   and controller reaction rather than treating 75/350 ms as a wire deadline.
   Confirm non-zero output is inhibited, OFF/zero remains transmittable, control
   integrators reset, alignment recovers, and there is no replay or recovery
   surge. Require a new validated combined-zero host phase before re-arm.
8. Verify drive feedback and then bounded wheel commands with the mower secured.
   Verify collision-reverse cannot run on untrusted feedback.
9. Verify blade communication with the cutting blade removed and independent
   rotor-motion observation. Controller-reported zero RPM is not by itself a
   physical-stop pass criterion.
10. Compare blade-temperature readings against an independent temperature
    measurement and, if needed, continuity-check PC2/PC3 before changing the
    configured ADC input.

Useful failure instrumentation: timestamped UART RX bytes and DMA/HAL results,
snapshot sequence/age/validity, recovery reason/count, controller error byte,
requested versus actually transmitted zero/non-zero command, watchdog
breadcrumb, USB connect/disconnect counters, and raw ADC samples.

## Remaining RM1000 questions

- Actual blade response length and frame schema.
- Drive/blade command or controller differences.
- Panel variant and actual panel traffic.
- Motor-controller reset and power behavior.
- Physical blade NTC pin and correct ADC channel.
- UART frames observed on this exact RM1000/controller revision.
- USB reset/re-enumeration behavior.
- SWO effect during startup on the real board.
- Actual controller action/watchdog and mechanical stop latency after the
  firmware emits zero/OFF under each injected link-failure mode.

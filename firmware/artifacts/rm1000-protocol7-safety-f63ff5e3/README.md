# RM1000 protocol-v7 safety review candidate

This artifact was built from the source commit named below. It is a review
candidate and has not been flashed to the mower.

## Source and target

- Fork: `liltaket/mowglinext-f401-rm1000`
- Branch: `feature/rm1000-protocol7-safety-candidate`
- Exact source commit: `f63ff5e311fa0bdb91ceab094bb7a290fa2519b9`
- Target/profile: `BiltemaRM1000_MPU6050_Yaw180`
- MCU: STM32F401VCT6, 84 MHz, 128 KiB application flash, 48 KiB RAM
- Orientation: `Yaw180`; native basis-vector tests cover accelerometer, gyro,
  and magnetometer paths.
- Protocol reference: upstream `dev` at
  `093d636dbdeada60939fd06a1f1b63645a45f789`; protocol v7 guard reports 18/18
  packet IDs and structures and a matching fingerprint.
- Custom application USB-DFU protocol: absent. The framework build may compile
  its general-purpose DFU library, but the final ELF has no DFU application
  symbols or entry handler.

## Build

Run from `firmware/stm32/ros_usbnode`:

```sh
pio run -e BiltemaRM1000_MPU6050_Yaw180 -t clean
pio run -e BiltemaRM1000_MPU6050_Yaw180
```

Build environment: PlatformIO Core 6.1.18, ST platform 19.7.1,
STM32CubeF4 1.28.1, ARM GNU toolchain 9.3.1 (`arm-none-eabi-gcc`), GNU binutils
from the PlatformIO ARM toolchain package. The build emitted the existing
PlatformIO note that it could not find a default `stm32f401vct6` startup file;
the project supplies its own startup code. No firmware compiler warnings were
reported.

## Validation recorded for this source commit

- PlatformIO native tests: 29/29 passed.
- PlatformIO `native_rm1000_yaw180`: 29/29 passed.
- Production blade-controller command harness: Yardforce500, Yardforce500B,
  RM1000 startup/commands, and coastdown validation passed.
- Onboard I2C recovery and heartbeat callback harness passed for both F401
  board variants.
- Soft-I2C recovery and `fw_params` harnesses passed.
- Protocol-v7 guard, board-default parity, ROS header sync check, and
  `git diff --check` passed.
- Clean RM1000 F401 target build passed.
- No mower actuation, blade actuation, charger/power-bus live test, or physical
  sensor-axis test was performed. PAC5223 startup response timing and the full
  RM1000 electrical behavior still need supervised hardware validation.

## Binary measurements

`firmware.bin` is 75,528 bytes. PlatformIO reports 75,096 bytes flash use
(57.3% of the 131,072-byte application region) and 17,108 bytes RAM use (34.8%
of 49,152 bytes). `arm-none-eabi-size` reports text 74,544, data 972, bss
17,684 bytes.

SHA-256:

- `firmware.bin`: `21229fa2bfc0004e77e4bbe37d2e41ad66573f13d921a9070470438b33cd7edf`
- `firmware.elf`: `2f9841ccd5c2081f06fd09eea65768426258cf0eb28176c470c1d07bd8d90ab3`

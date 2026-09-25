# RM1000 protocol-v7 safety review candidate

> Historical package for source `5b2205d8`. It is retained unchanged as a
> record of that build and is superseded for current review by
> [`rm1000-protocol7-arch-safety-596cf9c1`](../rm1000-protocol7-arch-safety-596cf9c1/README.md).
> Use the newer package when reviewing the shared actuator-authorization
> architecture; these older BIN/ELF files do not contain those later changes.

This package is the clean PlatformIO build of the exact committed source below.
It is a review artifact for the private `liltaket/mowglinext-f401-rm1000` fork;
it is not an upstream release and has not been flashed to the mower.

## Source and build

- Source commit: `5b2205d80509db97fa729c9665938df76541383a`
- Source branch: `feature/rm1000-protocol7-safety-candidate`
- Protocol reference: upstream `dev` at `093d636dbdeada60939fd06a1f1b63645a45f789`
- PlatformIO environment: `BiltemaRM1000_MPU6050_Yaw180`
- Build identity: firmware `1.11.66`, source `5b2205d8`, count `2882`
- PlatformIO ST `19.7.1`, STM32CubeF4 `1.28.1`, GNU Arm Embedded GCC `9.3.1`
- Build command: `pio run -e BiltemaRM1000_MPU6050_Yaw180`
- PlatformIO memory report: RAM `17,116 / 49,152` bytes (`34.8%`); flash `75,640 / 131,072` bytes (`57.7%`)

## Artifact checksums

- `firmware.bin` — 76,072 bytes; SHA-256: `2e7ffa943b9bbf2e49c1e535f4b834f895579d2c261939b271cff81f88ba6ea2`
- `firmware.elf` — SHA-256: `f93059af6b79ca36d19c009cdf94e074511e1fe0f77169b75a02bdacc0fe4565`

## Validation completed

- Clean RM1000/Yaw180 firmware build succeeded.
- PlatformIO `native` and `native_rm1000_yaw180`: `30/30` tests passed in each.
- Production blade harness passed for Yardforce500, Yardforce500B, BiltemaRM1000, and the coastdown-validation image. RM1000 cases include missing startup response, failed startup TX, bounded retry, and no overlapping RUN poll while RX DMA is active.
- Protocol guard: protocol v7, 18 IDs / 18 structs, fingerprint matched.
- Board default parity, ROS header sync, firmware parameter, onboard I2C recovery, and software I2C recovery checks passed.
- `arm-none-eabi-nm` found no application `enter_dfu` or DFU-request symbol.
- Independent source review found no remaining concrete blocker in the reviewed changes.

## Scope and limits

The source retains `BiltemaRM1000_MPU6050_Yaw180`, matches the v7 wire protocol,
and does not add the custom USB DFU command path. Automated tests cover firmware
logic and HAL-shim behavior; they do not establish live sensor readings, PAC5223
response timing, charger or power-bus behavior, or physical actuator behavior.
No motor or blade was started for this candidate. Hardware validation remains
pending before treating this as safe for deployment or upstream contribution.

# RM1000 protocol-v7 architecture/safety review candidate

This is a fresh build from the committed source listed below. It is a review
artifact for the fork-only draft PR, not an upstream release. It has not been
flashed or exercised on a physical mower.

## Source and build identity

- Source commit: `596cf9c1c42c8238976a84367208eaeda257c6e4`
- Source branch: `feature/rm1000-protocol7-safety-candidate`
- PlatformIO environment: `BiltemaRM1000_MPU6050_Yaw180`
- Firmware identity: `1.11.69` (source `596cf9c1`, commit count `2885`)
- Protocol: v7, compared with pinned upstream firmware reference
  `093d636dbdeada60939fd06a1f1b63645a45f789`
- Build toolchain: PlatformIO ST 19.7.1, STM32CubeF4 1.28.1, GNU Arm GCC 9.3.1
- Build: `pio run -e BiltemaRM1000_MPU6050_Yaw180` after cleaning all four target builds
- Size: RAM `17,132 / 49,152` bytes (34.9%); flash `76,152 / 131,072` bytes (58.1%)

## Checksums

- `firmware.bin` — 76,584 bytes; SHA-256:
  `3078983128117cebd1b68fbb91eb7ad0161a93067a9970e7c09d6257e130e149`
- `firmware.elf` — 266,892 bytes; SHA-256:
  `3f5d5d274ff1e8b14196a8bb225dcf8da26307c04136c7c0a752c5a6a650b85e`

## Validation evidence

- Clean full build passed for `Yardforce500`, `Yardforce500B`, `BiltemaRM1000`, and `BiltemaRM1000_MPU6050_Yaw180`.
- Native and Yaw180 native tests: 62/62 in each environment.
- Blade HAL harness passed Yardforce500, Yardforce500B, RM1000, and coastdown profiles, including stale authorization, emergency/IDLE/link boundaries, and startup TX recovery cases.
- Onboard I2C and heartbeat harness passed for F103 and F401 variants; software I2C recovery harness passed.
- Firmware parameter persistence harness passed; board-default parity passed.
- Protocol guard: v7, 18 packet IDs / 18 structs, pinned fingerprint matches. Firmware protocol header, host protocol header, and communication implementation have no diff from the pinned review base.
- The Yaw180 tests verify +X→−X, +Y→−Y, +Z→+Z for accelerometer and gyroscope, and XY negation with Z preserved for magnetometer.
- Firmware ELF symbol inspection found no application DFU request/entry symbol.

## Scope and limitations

Emergency, IDLE, and motor-link boundaries now invalidate one shared actuator
authorization epoch. A fresh zero-motion command is required before a later
non-zero drive command; blade authorization is rechecked at the lower UART/DMA
output. Charger ceiling and ADC freshness remain shared firmware behavior.

RM1000 remains a Yardforce500B/F401 specialization for the 900 ECO panel, blade
STOP/FWD/REV bytes `0x02/0x81/0xC1`, 20 ms polling, and sequenced blade power.
Yaw180 is a separate optional installation profile.

Automated/native and HAL-shim results do not establish live sensor quality,
PAC5223 or charger electrical behavior, TF4 polarity, or physical motor/blade
behavior. No physical actuator test or flash was performed for this candidate.
Do not treat this artifact as field-safe or upstream-ready without independent
review and the required hardware validation.

The firmware template now represents `BOARD_BILTEMA_RM1000`; the separate GUI
board type/provider/environment mapping is still required before RM1000 can be
selected through the GUI.

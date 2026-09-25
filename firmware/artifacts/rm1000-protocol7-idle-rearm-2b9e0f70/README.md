# RM1000 protocol-v7 idle re-arm candidate

This is a software review artifact for the Biltema RM1000 with the MPU-6050
installed in the Yaw180 orientation. It includes a narrow re-arm fix for the
case where drive authorization requires a fresh zero command but the firmware
previously rejected every command received in IDLE.

## Source and build identity

- Fork: `liltaket/mowglinext-f401-rm1000`
- Branch: `feature/rm1000-protocol7-safety-candidate`
- Source commit: `2b9e0f70f66a898ce4b2408938c6f89bffd48770`
- Firmware identity: `1.11.71`
- Protocol: v7; reference baseline `093d636dbdeada60939fd06a1f1b63645a45f789`
- PlatformIO environment: `BiltemaRM1000_MPU6050_Yaw180`
- Target: STM32F401VC, RM1000 profile, optional Yaw180 IMU transform

## Files and checksums

| File | Size | SHA-256 |
|---|---:|---|
| `firmware.bin` | 76,736 bytes | `ebcf40bdd9ce2165729391e04921d702ed348d7de9454d2b8eb337ca4ddcdd98` |
| `firmware.elf` | 266,892 bytes | `ff1fe1be7fd394a7d3b4796aa2803866b6a5b38a9c02bd0aabac7cb449b4ad4c` |

## Validation

- Native suite: 33/33 passed.
- `native_rm1000_yaw180`: 33/33 passed.
- PlatformIO builds passed for `Yardforce500`, `Yardforce500B`,
  `BiltemaRM1000`, and `BiltemaRM1000_MPU6050_Yaw180`.
- The idle-zero regression test checks that only a finite exact-zero command can
  establish the drive re-arm phase in IDLE, does not refresh the motion TTL or
  blade freshness, and still produces a zero PAC5210 request through the IDLE
  final-output gate.
- No firmware from this source commit has been flashed to the mower. The earlier
  `1.11.69` candidate was reported by the user to be unable to move or start the
  blade. At the last passive telemetry read the host still reported `IDLE` and
  charging active, so that state did not establish a successful actuator test
  or a definitive root cause.
- This artifact is for review and further supervised validation; it is not a
  claim of field readiness.

PlatformIO printed its known “Cannot find the default startup file” warning for
the generic STM32F401 target; the build completed successfully with the
project's startup source.

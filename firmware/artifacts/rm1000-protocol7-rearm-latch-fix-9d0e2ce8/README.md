# RM1000 protocol-v7 re-arm latch fix

This package contains the F401 build produced from source commit
`9d0e2ce82d83c553c1b336bf531a8c72db03d2ab` in environment
`BiltemaRM1000_MPU6050_Yaw180`.

Firmware identity: `1.11.73` (protocol 7). The change limits the atomic
zero/off revalidation to the transition from re-arm-required to armed. Healthy
links and the existing emergency, watchdog, authorization, and final-output
checks remain responsible for inhibiting later faults.

## Files

| File | Size | SHA-256 |
|---|---:|---|
| `firmware.bin` | 76,784 bytes | `9cc0aead7a9c189260a5d595263c6b1e4f7786095312091bc91fddbb026f23c3` |
| `firmware.elf` | 266,908 bytes | `0c284902af9667daee7802c657e15023d31eaaba7c0967a9b61305270592501a` |

## Validation

- `pio test -e native -e native_rm1000_yaw180`: 68/68 tests passed.
- `pio run -e BiltemaRM1000_MPU6050_Yaw180`: build passed; 76,352 bytes flash
  and 17,132 bytes RAM used by the linked image.
- This artifact has not been flashed or physically tested. It does not establish
  RM1000 actuator or field safety.

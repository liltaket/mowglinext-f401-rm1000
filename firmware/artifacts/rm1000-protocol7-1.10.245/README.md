# RM1000 STM32F401 protocol-v7 candidate (1.10.245)

This directory contains the exact firmware build flashed during the supervised
RM1000 protocol-v7 compatibility check. It is a review candidate, not a stable
release.

## Provenance

- Firmware source commit: `447124d957f0101b2dc70afc963744f517883960`
- Profile: `BiltemaRM1000_MPU6050_Yaw180`
- Target: STM32F401VC
- Protocol: 7
- BIN: `rm1000-f401-protocol7-no-dfu-1.10.245.bin` (71,848 bytes)
- BIN SHA-256: `fa849700a1e6b2b66923df7fe7e583b3ec9c8a57fc974b80ce1a97e5ae40c4ff`
- ELF: `rm1000-f401-protocol7-no-dfu-1.10.245.elf` (262,724 bytes)
- ELF SHA-256: `f3fc904386b622db4585e6809d6d9c23623058564f204e9ca9e0018cb2b9d3df`

The BIN and ELF were built from the source commit above. This artifact-only
addition does not change the firmware source or its build inputs.

## Validation evidence

- Protocol fingerprint: 18 packet IDs and 18 packet structures match host and
  firmware; native protocol guard passed.
- Native firmware tests: 6/6 passed.
- RM1000 PlatformIO target build and board-default parity checks passed.
- C++ format and `git diff --check` passed for the candidate source changes.
- OpenOCD reported `Verified OK`; an independent readback of the programmed
  image had the same SHA-256 as the BIN above.
- After reboot, the live bridge reported firmware 1.10.245, protocol 7, and
  `firmware_compatible: true`.
- Passive telemetry and sensor streams were observed. No drive motors, blade
  motor, or charging controller were actuated. No sensor/button stimuli were
  performed. LIDAR had no active publisher, battery percentage was unknown,
  and some board inputs could only be inspected passively.

This is not a full safety or upstream-readiness review. Active actuator/HIL,
field behavior, and independent source review remain outstanding. See the PR
for the change summary and review discussion.

# Biltema RM1000 firmware and host profile

This records the profile observed on Bruno's working Biltema RM1000 on
2026-09-27. It is a starting preset for that robot; it is not a generic
calibration for every RM1000.

## Firmware target

| Setting | Value |
|---|---|
| Mainboard | RM-MB V6.1, STM32F401VC |
| PlatformIO target | `BiltemaRM1000` |
| Panel | Yardforce 900 ECO protocol |
| Blade control | Sequenced inverter power; RM1000 command values; 20 ms poll |
| Protocol | Mowgli protocol 7 |
| Prebuilt release | Not published; use source build only until an RM1000 image is intentionally released |

`BiltemaRM1000_MPU6050_Yaw180` adds a 180° Z transform for the external IMU.
It is specific to the observed mounting and should not be selected for an
identity-mounted sensor.

## Host drive values observed on this robot

These values came from the live host-side drive tuning profile. They are
seeded by the `BiltemaRM1000` mower preset in the GUI:

| Parameter | Value | Meaning |
|---|---:|---|
| `ticks_per_meter` | 331.6 | Host wheel-odometry scale, pushed to firmware at connection |
| `wheel_track` | 0.325 m | Differential-drive wheel separation |
| `wheel_pid_pwm_per_mps` | 350 | Feedforward scale |
| `wheel_pid_kp` | 10 | Proportional gain |
| `wheel_pid_ki` | 0.06 | Integral gain |
| `wheel_pid_kd` | 0 | Derivative gain |
| `wheel_pid_integral_limit` | 45 | Integral clamp |

These settings were observed while the wheel-drive path was reported working.
The model preset deliberately leaves chassis dimensions, sensor placement,
blade geometry, and battery limits alone; no reliable RM1000-specific
measurements for those values were available in this record. Existing values
in the installed `mowgli_robot.yaml` therefore remain authoritative for them.

The STM32 board's compile-time `TICKS_PER_M` is only its brief power-on
fallback. The ROS2 host sends the saved runtime value over protocol 7 after
connection. The PID values above are host/firmware runtime parameters, not the
separate temporary STM32 RAM-trace values used during earlier diagnostics.

## Scope and validation boundary

This record documents the starting values added by the GUI preset and the
source-build board selection. The preset does not change runtime motor logic,
sensor handling, or safety behavior. The GUI selects the source-build path and
does not offer a prebuilt RM1000 image.

The host-side values above were observed on this robot while wheel drive was
reported working. They are initial values for this robot, not validation of
other RM1000 units. The preset changes themselves have not been hardware-tested;
firmware CI/build evidence belongs to the firmware target PR and does not
validate the GUI profile.

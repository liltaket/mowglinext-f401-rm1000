// Copyright 2026 Mowgli Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOWGLI_HARDWARE__BLADE_TELEMETRY_HPP_
#define MOWGLI_HARDWARE__BLADE_TELEMETRY_HPP_

#include "mowgli_hardware/ll_datatypes.hpp"

namespace mowgli_hardware
{

constexpr float blade_current_amps(const LlBladeStatus& packet)
{
  // Legacy boards forward ESC UART bytes 9-10 unchanged in milliamps. Match
  // the ROS1 conversion to amperes; this field is not electrical power and
  // must not be divided by volts.
  return static_cast<float>(packet.power_watts) / 1000.0f;
}

constexpr float blade_power_watts(const LlBladeStatus& packet)
{
  return static_cast<float>(packet.power_watts) / 10.0f;
}

constexpr float blade_current_amps_from_power(const LlBladeStatus& packet,
                                              const float system_voltage)
{
  return system_voltage > 0.0f ? blade_power_watts(packet) / system_voltage : 0.0f;
}

}  // namespace mowgli_hardware

#endif  // MOWGLI_HARDWARE__BLADE_TELEMETRY_HPP_

// Copyright 2026 Mowgli Project
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MOWGLI_HARDWARE__BLADE_TELEMETRY_HPP_
#define MOWGLI_HARDWARE__BLADE_TELEMETRY_HPP_

#include "mowgli_hardware/ll_datatypes.hpp"

namespace mowgli_hardware
{

constexpr float blade_current_amps(const LlBladeStatus& packet)
{
  // Every firmware target normalizes its controller-specific feedback to the
  // shared wire contract: milliamps. The legacy field name is misleading.
  return static_cast<float>(packet.power_watts) / 1000.0f;
}

constexpr bool blade_telemetry_contract_compatible(const uint8_t protocol_version,
                                                   const uint8_t active_flags)
{
  return protocol_version == kMowgliProtocolVersion &&
         (active_flags & CONFIG_CAPABILITY_LEGACY_BLADE_POWER_DECIWATTS) == 0u;
}

}  // namespace mowgli_hardware

#endif  // MOWGLI_HARDWARE__BLADE_TELEMETRY_HPP_

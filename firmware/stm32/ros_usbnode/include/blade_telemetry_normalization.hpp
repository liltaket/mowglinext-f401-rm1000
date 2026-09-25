#ifndef BLADE_TELEMETRY_NORMALIZATION_HPP_
#define BLADE_TELEMETRY_NORMALIZATION_HPP_

#include <cmath>
#include <cstdint>
#include <limits>

namespace mowgli_blade_telemetry
{

/** Convert PAC5223 blade power in 0.1 W units to the shared wire contract: mA. */
inline uint16_t deciwatts_to_milliamps(const uint16_t power_deciwatts,
                                       const float system_voltage)
{
  if (!std::isfinite(system_voltage) || system_voltage <= 1.0f)
  {
    return 0u;
  }

  const float current_milliamps =
      static_cast<float>(power_deciwatts) * 100.0f / system_voltage;
  if (!std::isfinite(current_milliamps) || current_milliamps <= 0.0f)
  {
    return 0u;
  }
  if (current_milliamps >= static_cast<float>(std::numeric_limits<uint16_t>::max()))
  {
    return std::numeric_limits<uint16_t>::max();
  }
  return static_cast<uint16_t>(current_milliamps + 0.5f);
}

}  // namespace mowgli_blade_telemetry

#endif  // BLADE_TELEMETRY_NORMALIZATION_HPP_

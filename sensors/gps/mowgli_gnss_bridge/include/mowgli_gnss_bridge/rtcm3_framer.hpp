// Copyright 2026 Mowgli Project
//
// SPDX-License-Identifier: GPL-3.0

#ifndef MOWGLI_GNSS_BRIDGE__RTCM3_FRAMER_HPP_
#define MOWGLI_GNSS_BRIDGE__RTCM3_FRAMER_HPP_

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mowgli_gnss_bridge
{

/// Incrementally extracts complete, CRC24Q-verified RTCM3 frames from a byte
/// stream. The input buffer is deliberately bounded: callers must never carry
/// correction bytes into a new TCP connection.
class Rtcm3Framer
{
public:
  static constexpr std::size_t kMaxPayloadBytes = 1023;
  static constexpr std::size_t kMaxFrameBytes = kMaxPayloadBytes + 6;
  static constexpr std::size_t kReceiveBufferBytes = 2048;
  static constexpr std::size_t kMaxBufferedBytes = kMaxFrameBytes + kReceiveBufferBytes;

  void append(const std::uint8_t* data, std::size_t size);
  bool popFrame(std::vector<std::uint8_t>* frame);
  void reset();

  static std::uint32_t crc24q(const std::uint8_t* data, std::size_t size);

private:
  std::vector<std::uint8_t> buffer_;
};

}  // namespace mowgli_gnss_bridge

#endif  // MOWGLI_GNSS_BRIDGE__RTCM3_FRAMER_HPP_

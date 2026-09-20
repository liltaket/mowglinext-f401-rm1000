// Copyright 2026 Mowgli Project
//
// SPDX-License-Identifier: GPL-3.0

#include "mowgli_gnss_bridge/rtcm3_framer.hpp"

#include <algorithm>

namespace mowgli_gnss_bridge
{

void Rtcm3Framer::append(const std::uint8_t* data, const std::size_t size)
{
  if (data == nullptr || size == 0)
  {
    return;
  }

  // A valid frame is <= 1029 bytes. Retain at most one possible frame plus a
  // leading preamble while resynchronising after a noisy/broken TCP peer.
  if (size >= kMaxBufferedBytes)
  {
    buffer_.assign(data + size - kMaxBufferedBytes, data + size);
    return;
  }
  if (buffer_.size() + size > kMaxBufferedBytes)
  {
    buffer_.erase(buffer_.begin(),
                  buffer_.begin() +
                      static_cast<std::ptrdiff_t>(buffer_.size() + size - kMaxBufferedBytes));
  }
  buffer_.insert(buffer_.end(), data, data + size);
}

bool Rtcm3Framer::popFrame(std::vector<std::uint8_t>* frame)
{
  if (frame == nullptr)
  {
    return false;
  }

  while (true)
  {
    const auto preamble = std::find(buffer_.begin(), buffer_.end(), 0xD3);
    if (preamble == buffer_.end())
    {
      buffer_.clear();
      return false;
    }
    if (preamble != buffer_.begin())
    {
      buffer_.erase(buffer_.begin(), preamble);
    }
    if (buffer_.size() < 3)
    {
      return false;
    }

    // RTCM3 reserves the upper six bits of byte 1. A non-zero value cannot be
    // a frame header, so discard this preamble and continue resynchronising.
    if ((buffer_[1] & 0xFCU) != 0U)
    {
      buffer_.erase(buffer_.begin());
      continue;
    }
    const std::size_t payload_bytes =
        (static_cast<std::size_t>(buffer_[1] & 0x03U) << 8U) | buffer_[2];
    const std::size_t frame_bytes = payload_bytes + 6U;
    if (frame_bytes > kMaxFrameBytes)
    {
      buffer_.erase(buffer_.begin());
      continue;
    }
    if (buffer_.size() < frame_bytes)
    {
      return false;
    }

    const std::uint32_t expected = (static_cast<std::uint32_t>(buffer_[frame_bytes - 3]) << 16U) |
                                   (static_cast<std::uint32_t>(buffer_[frame_bytes - 2]) << 8U) |
                                   buffer_[frame_bytes - 1];
    if (crc24q(buffer_.data(), frame_bytes - 3) != expected)
    {
      // Do not discard the rest of the candidate: it may contain a valid D3
      // following a corrupted frame start.
      buffer_.erase(buffer_.begin());
      continue;
    }

    frame->assign(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_bytes));
    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(frame_bytes));
    return true;
  }
}

void Rtcm3Framer::reset()
{
  buffer_.clear();
}

std::uint32_t Rtcm3Framer::crc24q(const std::uint8_t* data, const std::size_t size)
{
  std::uint32_t crc = 0;
  for (std::size_t i = 0; i < size; ++i)
  {
    crc ^= static_cast<std::uint32_t>(data[i]) << 16U;
    for (int bit = 0; bit < 8; ++bit)
    {
      crc <<= 1U;
      if ((crc & 0x1000000U) != 0U)
      {
        crc ^= 0x1864CFBU;
      }
    }
  }
  return crc & 0xFFFFFFU;
}

}  // namespace mowgli_gnss_bridge

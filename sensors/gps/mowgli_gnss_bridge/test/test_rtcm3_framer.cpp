// Copyright 2026 Mowgli Project
// SPDX-License-Identifier: GPL-3.0

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "mowgli_gnss_bridge/rtcm3_framer.hpp"

namespace
{

std::vector<std::uint8_t> makeFrame(const std::vector<std::uint8_t>& payload)
{
  std::vector<std::uint8_t> frame{0xD3,
                                  static_cast<std::uint8_t>((payload.size() >> 8U) & 0x03U),
                                  static_cast<std::uint8_t>(payload.size() & 0xFFU)};
  frame.insert(frame.end(), payload.begin(), payload.end());
  const auto crc = mowgli_gnss_bridge::Rtcm3Framer::crc24q(frame.data(), frame.size());
  frame.push_back(static_cast<std::uint8_t>(crc >> 16U));
  frame.push_back(static_cast<std::uint8_t>(crc >> 8U));
  frame.push_back(static_cast<std::uint8_t>(crc));
  return frame;
}

TEST(Rtcm3FramerTest, ReassemblesFragmentedFrame)
{
  const auto expected = makeFrame({0x43, 0x50, 0x10, 0x20});
  mowgli_gnss_bridge::Rtcm3Framer framer;
  std::vector<std::uint8_t> actual;
  framer.append(expected.data(), 2);
  EXPECT_FALSE(framer.popFrame(&actual));
  framer.append(expected.data() + 2, expected.size() - 2);
  ASSERT_TRUE(framer.popFrame(&actual));
  EXPECT_EQ(actual, expected);
}

TEST(Rtcm3FramerTest, RejectsBadCrcAndResynchronises)
{
  auto corrupt = makeFrame({0x01, 0x02});
  corrupt.back() ^= 0x01U;
  const auto expected = makeFrame({0x43, 0x50});
  corrupt.insert(corrupt.end(), expected.begin(), expected.end());
  mowgli_gnss_bridge::Rtcm3Framer framer;
  std::vector<std::uint8_t> actual;
  framer.append(corrupt.data(), corrupt.size());
  ASSERT_TRUE(framer.popFrame(&actual));
  EXPECT_EQ(actual, expected);
}

TEST(Rtcm3FramerTest, ResetDropsPartialFrameAcrossReconnect)
{
  const auto expected = makeFrame({0x11, 0x22, 0x33});
  mowgli_gnss_bridge::Rtcm3Framer framer;
  std::vector<std::uint8_t> actual;
  framer.append(expected.data(), 4);
  framer.reset();
  framer.append(expected.data() + 4, expected.size() - 4);
  EXPECT_FALSE(framer.popFrame(&actual));
}

TEST(Rtcm3FramerTest, KeepsFrameWhenLargestPartialAndReceiveAreCoalesced)
{
  std::vector<std::uint8_t> payload(mowgli_gnss_bridge::Rtcm3Framer::kMaxPayloadBytes, 0x42);
  const auto expected = makeFrame(payload);
  ASSERT_EQ(expected.size(), mowgli_gnss_bridge::Rtcm3Framer::kMaxFrameBytes);

  mowgli_gnss_bridge::Rtcm3Framer framer;
  std::vector<std::uint8_t> actual;
  framer.append(expected.data(), expected.size() - 1);
  std::vector<std::uint8_t> next_receive{expected.back()};
  next_receive.resize(mowgli_gnss_bridge::Rtcm3Framer::kReceiveBufferBytes, 0x00);
  framer.append(next_receive.data(), next_receive.size());

  ASSERT_TRUE(framer.popFrame(&actual));
  EXPECT_EQ(actual, expected);
}

}  // namespace

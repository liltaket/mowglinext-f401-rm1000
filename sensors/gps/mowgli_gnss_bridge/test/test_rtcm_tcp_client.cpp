// Copyright 2026 Mowgli Project
// SPDX-License-Identifier: GPL-3.0

// clang-format off
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
// clang-format on

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "mowgli_gnss_bridge/rtcm3_framer.hpp"
#include "mowgli_gnss_bridge/rtcm_tcp_client.hpp"

namespace
{

std::vector<std::uint8_t> makeFrame(const std::vector<std::uint8_t> &payload)
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

int listenLocalhost(std::uint16_t *port)
{
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return -1;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(fd, 2) != 0)
  {
    ::close(fd);
    return -1;
  }
  socklen_t address_size = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &address_size) != 0)
  {
    ::close(fd);
    return -1;
  }
  *port = ntohs(address.sin_port);
  return fd;
}

TEST(RtcmTcpClientTest, ReconnectDropsPartialFrameAndPublishesNextCompleteFrame)
{
  std::uint16_t port = 0;
  const int listener = listenLocalhost(&port);
  ASSERT_GE(listener, 0);
  const auto expected = makeFrame({0x43, 0x50, 0x10, 0x20});
  std::thread server(
      [listener, &expected]()
      {
        const int first = ::accept(listener, nullptr, nullptr);
        ASSERT_GE(first, 0);
        ASSERT_GT(::send(first, expected.data(), expected.size() - 1, 0), 0);
        ::close(first);
        const int second = ::accept(listener, nullptr, nullptr);
        ASSERT_GE(second, 0);
        ASSERT_GT(::send(second, expected.data(), expected.size(), 0), 0);
        ::close(second);
        ::close(listener);
      });

  int stop_pipe[2]{};
  ASSERT_EQ(::pipe(stop_pipe), 0);
  std::atomic_bool stopping{false};
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<std::uint8_t> received;
  mowgli_gnss_bridge::RtcmTcpClient client("127.0.0.1", port, 500, 10);
  std::thread worker(
      [&]()
      {
        client.run(stopping,
                   stop_pipe[0],
                   [&](std::vector<std::uint8_t> frame)
                   {
                     {
                       std::lock_guard<std::mutex> lock(mutex);
                       received = std::move(frame);
                     }
                     stopping.store(true);
                     const std::uint8_t stop = 1;
                     if (::write(stop_pipe[1], &stop, sizeof(stop)) !=
                         static_cast<ssize_t>(sizeof(stop)))
                     {
                       ADD_FAILURE() << "failed to wake RTCM TCP client";
                     }
                     condition.notify_one();
                   });
      });

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(lock,
                                   std::chrono::seconds(2),
                                   [&received]()
                                   {
                                     return !received.empty();
                                   }));
  }
  worker.join();
  server.join();
  ::close(stop_pipe[0]);
  ::close(stop_pipe[1]);
  EXPECT_EQ(received, expected);
}

TEST(RtcmTcpClientTest, StopPipeCancelsReconnectWithoutWaitingForTimeout)
{
  int stop_pipe[2]{};
  ASSERT_EQ(::pipe(stop_pipe), 0);
  std::atomic_bool stopping{false};
  mowgli_gnss_bridge::RtcmTcpClient client("127.0.0.1", 1, 5000, 5000);
  const auto started = std::chrono::steady_clock::now();
  std::thread worker(
      [&]()
      {
        client.run(stopping, stop_pipe[0], [](auto) {});
      });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  stopping.store(true);
  const std::uint8_t stop = 1;
  ASSERT_EQ(::write(stop_pipe[1], &stop, sizeof(stop)), 1);
  worker.join();
  const auto elapsed = std::chrono::steady_clock::now() - started;
  ::close(stop_pipe[0]);
  ::close(stop_pipe[1]);
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

}  // namespace

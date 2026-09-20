// Copyright 2026 Mowgli Project
//
// SPDX-License-Identifier: GPL-3.0

// clang-format off
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
// clang-format on

#include "mowgli_gnss_bridge/rtcm_tcp_client.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <utility>

#include "mowgli_gnss_bridge/rtcm3_framer.hpp"

namespace mowgli_gnss_bridge
{

RtcmTcpClient::RtcmTcpClient(std::string host,
                             const int port,
                             const int connect_timeout_ms,
                             const int reconnect_delay_ms)
    : host_(std::move(host)),
      port_(port),
      connect_timeout_ms_(connect_timeout_ms),
      reconnect_delay_ms_(reconnect_delay_ms)
{
}

void RtcmTcpClient::run(const std::atomic_bool& stop_requested,
                        const int stop_fd,
                        const FrameCallback& on_frame) const
{
  while (!stop_requested.load())
  {
    const bool stopped = connectAndConsume(stop_requested, stop_fd, on_frame);
    if (stopped || stop_requested.load())
    {
      return;
    }
    if (waitForStopOrTimeout(stop_requested, stop_fd, reconnect_delay_ms_))
    {
      return;
    }
  }
}

bool RtcmTcpClient::connectAndConsume(const std::atomic_bool& stop_requested,
                                      const int stop_fd,
                                      const FrameCallback& on_frame) const
{
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* addresses = nullptr;
  const std::string port = std::to_string(port_);
  if (::getaddrinfo(host_.c_str(), port.c_str(), &hints, &addresses) != 0)
  {
    return false;
  }

  // This deadline is shared by every address returned by DNS. A dual-stack
  // hostname must not consume connect_timeout_ms once per address.
  const auto connect_deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(connect_timeout_ms_);
  int socket_fd = -1;
  bool stopped = false;
  for (addrinfo* address = addresses; address != nullptr && !stopped; address = address->ai_next)
  {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                               connect_deadline - std::chrono::steady_clock::now())
                               .count();
    if (remaining <= 0)
    {
      break;
    }
    socket_fd = ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (socket_fd < 0)
    {
      continue;
    }
    const int flags = ::fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
      ::close(socket_fd);
      socket_fd = -1;
      continue;
    }
    const int connect_result = ::connect(socket_fd, address->ai_addr, address->ai_addrlen);
    if (connect_result != 0 && errno != EINPROGRESS)
    {
      ::close(socket_fd);
      socket_fd = -1;
      continue;
    }
    if (connect_result != 0)
    {
      pollfd descriptors[2]{{socket_fd, POLLOUT, 0}, {stop_fd, POLLIN, 0}};
      const int ready = ::poll(descriptors, 2, static_cast<int>(remaining));
      if (ready > 0 && (descriptors[1].revents & POLLIN) != 0)
      {
        stopped = true;
      }
      int socket_error = 0;
      socklen_t socket_error_size = sizeof(socket_error);
      if (stopped || ready <= 0 || (descriptors[0].revents & POLLOUT) == 0 ||
          ::getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) != 0 ||
          socket_error != 0)
      {
        ::close(socket_fd);
        socket_fd = -1;
        continue;
      }
    }
    break;
  }
  ::freeaddrinfo(addresses);
  if (stopped || socket_fd < 0)
  {
    return stopped;
  }

  Rtcm3Framer framer;
  std::array<std::uint8_t, Rtcm3Framer::kReceiveBufferBytes> receive_buffer{};
  while (!stop_requested.load())
  {
    pollfd descriptors[2]{{socket_fd, POLLIN, 0}, {stop_fd, POLLIN, 0}};
    const int ready = ::poll(descriptors, 2, 1000);
    if (ready > 0 && (descriptors[1].revents & POLLIN) != 0)
    {
      stopped = true;
      break;
    }
    if (ready < 0 || (descriptors[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
    {
      break;
    }
    if (ready == 0 || (descriptors[0].revents & POLLIN) == 0)
    {
      continue;
    }
    const ssize_t received = ::recv(socket_fd, receive_buffer.data(), receive_buffer.size(), 0);
    if (received <= 0)
    {
      break;
    }
    framer.append(receive_buffer.data(), static_cast<std::size_t>(received));
    std::vector<std::uint8_t> frame;
    while (framer.popFrame(&frame))
    {
      on_frame(std::move(frame));
    }
  }
  ::close(socket_fd);
  return stopped;
}

bool RtcmTcpClient::waitForStopOrTimeout(const std::atomic_bool& stop_requested,
                                         const int stop_fd,
                                         const int timeout_ms) const
{
  if (stop_requested.load())
  {
    return true;
  }
  pollfd descriptor{stop_fd, POLLIN, 0};
  return ::poll(&descriptor, 1, timeout_ms) > 0 && (descriptor.revents & POLLIN) != 0;
}

}  // namespace mowgli_gnss_bridge

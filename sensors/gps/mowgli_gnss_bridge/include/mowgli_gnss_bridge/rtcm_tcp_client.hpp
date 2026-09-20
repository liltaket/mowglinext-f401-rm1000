// Copyright 2026 Mowgli Project
//
// SPDX-License-Identifier: GPL-3.0

#ifndef MOWGLI_GNSS_BRIDGE__RTCM_TCP_CLIENT_HPP_
#define MOWGLI_GNSS_BRIDGE__RTCM_TCP_CLIENT_HPP_

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mowgli_gnss_bridge
{

/// A cancellable, reconnecting RTCM3 TCP consumer. The caller owns the stop
/// pipe's write end; this worker is the sole owner of all TCP socket closes.
class RtcmTcpClient
{
public:
  using FrameCallback = std::function<void(std::vector<std::uint8_t>)>;

  RtcmTcpClient(std::string host, int port, int connect_timeout_ms, int reconnect_delay_ms);

  /// Blocks until stop_requested is set or stop_fd becomes readable.
  void run(const std::atomic_bool& stop_requested,
           int stop_fd,
           const FrameCallback& on_frame) const;

private:
  bool connectAndConsume(const std::atomic_bool& stop_requested,
                         int stop_fd,
                         const FrameCallback& on_frame) const;
  bool waitForStopOrTimeout(const std::atomic_bool& stop_requested,
                            int stop_fd,
                            int timeout_ms) const;

  std::string host_;
  int port_;
  int connect_timeout_ms_;
  int reconnect_delay_ms_;
};

}  // namespace mowgli_gnss_bridge

#endif  // MOWGLI_GNSS_BRIDGE__RTCM_TCP_CLIENT_HPP_

// Copyright 2026 Mowgli Project
//
// SPDX-License-Identifier: GPL-3.0

// clang-format off
#include <unistd.h>
// clang-format on

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mowgli_gnss_bridge/rtcm_tcp_client.hpp"
#include "rclcpp/rclcpp.hpp"
#include "universal_gnss_ros2/msg/rtcm_frame.hpp"

namespace mowgli_gnss_bridge
{
namespace
{

using RtcmFrame = universal_gnss_ros2::msg::RtcmFrame;

class RtcmTcpSource : public rclcpp::Node
{
public:
  RtcmTcpSource()
      : Node("rtcm_tcp_source"),
        host_(declare_parameter<std::string>("host", "")),
        port_(declare_parameter<int>("port", 0)),
        connect_timeout_ms_(declare_parameter<int>("connect_timeout_ms", 3000)),
        reconnect_delay_ms_(declare_parameter<int>("reconnect_delay_ms", 1000)),
        output_topic_(
            declare_parameter<std::string>("output_rtcm_topic", "/_gps_internal/universal/rtcm"))
  {
    if (host_.empty() || port_ < 1 || port_ > 65535 || connect_timeout_ms_ < 50 ||
        reconnect_delay_ms_ < 50)
    {
      throw std::runtime_error(
          "host, port (1..65535), connect_timeout_ms (>=50), and reconnect_delay_ms (>=50) are "
          "required");
    }
    int stop_pipe[2]{};
    if (::pipe(stop_pipe) != 0)
    {
      throw std::runtime_error("failed to create RTCM TCP stop pipe");
    }
    stop_read_fd_ = stop_pipe[0];
    stop_write_fd_ = stop_pipe[1];
    publisher_ = create_publisher<RtcmFrame>(output_topic_,
                                             rclcpp::QoS(10).reliable().durability_volatile());
    worker_ = std::thread(&RtcmTcpSource::run, this);
  }

  ~RtcmTcpSource() override
  {
    stopping_.store(true);
    const std::uint8_t stop = 1;
    // The worker alone owns close(socket_fd). This pipe only wakes its poll.
    if (::write(stop_write_fd_, &stop, sizeof(stop)) != static_cast<ssize_t>(sizeof(stop)))
    {
      RCLCPP_WARN(get_logger(), "failed to wake RTCM TCP worker during shutdown");
    }
    if (worker_.joinable())
    {
      worker_.join();
    }
    ::close(stop_read_fd_);
    ::close(stop_write_fd_);
  }

private:
  void run()
  {
    RtcmTcpClient client(host_, port_, connect_timeout_ms_, reconnect_delay_ms_);
    client.run(stopping_,
               stop_read_fd_,
               [this](std::vector<std::uint8_t> frame)
               {
                 RtcmFrame message;
                 message.data = std::move(frame);
                 publisher_->publish(message);
               });
  }

  const std::string host_;
  const int port_;
  const int connect_timeout_ms_;
  const int reconnect_delay_ms_;
  const std::string output_topic_;
  rclcpp::Publisher<RtcmFrame>::SharedPtr publisher_;
  std::atomic_bool stopping_{false};
  int stop_read_fd_{-1};
  int stop_write_fd_{-1};
  std::thread worker_;
};

}  // namespace
}  // namespace mowgli_gnss_bridge

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<mowgli_gnss_bridge::RtcmTcpSource>());
  rclcpp::shutdown();
  return 0;
}

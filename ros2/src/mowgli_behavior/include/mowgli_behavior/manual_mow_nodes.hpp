// Copyright 2026 Mowgli Project
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <optional>
#include <string>

#include "behaviortree_cpp/behavior_tree.h"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "mowgli_behavior/bt_context.hpp"
#include "mowgli_interfaces/srv/mower_control.hpp"
#include "rclcpp/rclcpp.hpp"

namespace mowgli_behavior
{

/// Starts manual mowing only after a fresh firmware OFF report and a bounded
/// exact-zero re-arm phase. The service response is host acceptance only;
/// success is held RUNNING until firmware telemetry reports active RPM.
///
/// A failed start remains latched in a zero-velocity safe state until this
/// node is halted by its parent ReactiveSequence after the command changes.
/// This prevents a failed blade start from being retried at BT tick rate.
class ManualMowBladeStart : public BT::StatefulActionNode
{
public:
  using MowerControl = mowgli_interfaces::srv::MowerControl;
  using RequestFuture = rclcpp::Client<MowerControl>::FutureAndRequestId;

  ManualMowBladeStart(const std::string& name, const BT::NodeConfig& config)
      : BT::StatefulActionNode(name, config)
  {
  }

  static BT::PortsList providedPorts()
  {
    return {};
  }

  BT::NodeStatus onStart() override;
  BT::NodeStatus onRunning() override;
  void onHalted() override;

private:
  enum class Phase
  {
    Idle,
    WaitingForOffResponse,
    WaitingForOffTelemetry,
    WaitingForModePropagation,
    WaitingForOnResponse,
    WaitingForActiveTelemetry,
    Active,
    Fault,
  };

  struct StatusSnapshot
  {
    mowgli_interfaces::msg::Status status;
    std::chrono::steady_clock::time_point received_at{};
  };

  static constexpr auto kStatusMaxAge = std::chrono::milliseconds(1000);
  static constexpr auto kBladeSampleMaxAge = std::chrono::milliseconds(1000);
  static constexpr auto kServiceTimeout = std::chrono::seconds(2);
  static constexpr auto kOffTelemetryTimeout = std::chrono::seconds(3);
  static constexpr auto kActiveTelemetryTimeout = std::chrono::seconds(4);
  static constexpr auto kMinimumRearmPrelude = std::chrono::milliseconds(750);
  static constexpr auto kModePropagationDelay = std::chrono::milliseconds(100);

  void initializeIo(const std::shared_ptr<BTContext>& ctx);
  void publishHighLevelStatus(const std::shared_ptr<BTContext>& ctx,
                              uint8_t state,
                              const std::string& state_name);
  void publishZero(const std::shared_ptr<BTContext>& ctx);
  StatusSnapshot statusSnapshot(const std::shared_ptr<BTContext>& ctx) const;
  bool hasFreshCompatibleStatus(const StatusSnapshot& snapshot) const;
  bool hasFreshBladeSample(const StatusSnapshot& snapshot,
                           const std::shared_ptr<BTContext>& ctx) const;
  static int64_t bladeStampNs(const mowgli_interfaces::msg::Status& status);
  bool sendBladeRequest(const std::shared_ptr<BTContext>& ctx, bool enabled);
  void sendBestEffortOff(const std::shared_ptr<BTContext>& ctx);
  void removePendingRequest();
  void enterFault(const std::shared_ptr<BTContext>& ctx, const std::string& reason);

  rclcpp::Publisher<geometry_msgs::msg::TwistStamped>::SharedPtr zero_publisher_;
  rclcpp::Client<MowerControl>::SharedPtr blade_client_;
  std::optional<RequestFuture> pending_request_;
  Phase phase_{Phase::Idle};
  std::chrono::steady_clock::time_point phase_started_at_{};
  std::chrono::steady_clock::time_point rearm_started_at_{};
  std::chrono::steady_clock::time_point mode_published_at_{};
  int64_t off_response_stamp_ns_{0};
  int64_t on_request_stamp_ns_{0};
};

}  // namespace mowgli_behavior

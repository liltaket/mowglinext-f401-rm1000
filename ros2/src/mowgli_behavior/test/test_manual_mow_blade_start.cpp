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
//
// SPDX-License-Identifier: GPL-3.0

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "behaviortree_cpp/bt_factory.h"
#include "geometry_msgs/msg/twist_stamped.hpp"
#include "mowgli_behavior/bt_context.hpp"
#include "mowgli_behavior/manual_mow_nodes.hpp"
#include <gtest/gtest.h>

using namespace std::chrono_literals;
using mowgli_behavior::BTContext;
using mowgli_behavior::ManualMowBladeStart;
using HighLevelStatus = mowgli_interfaces::msg::HighLevelStatus;
using MowerControl = mowgli_interfaces::srv::MowerControl;
using Status = mowgli_interfaces::msg::Status;

namespace
{

class RclcppEnvironment : public ::testing::Environment
{
public:
  void SetUp() override
  {
    if (!rclcpp::ok())
      rclcpp::init(0, nullptr);
  }

  void TearDown() override
  {
    rclcpp::shutdown();
  }
};

::testing::Environment* const rclcpp_env =
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment());

class ManualCommand : public BT::ConditionNode
{
public:
  ManualCommand(const std::string& name, const BT::NodeConfig& config)
      : BT::ConditionNode(name, config)
  {
  }

  static BT::PortsList providedPorts()
  {
    return {};
  }

  BT::NodeStatus tick() override
  {
    const auto ctx = config().blackboard->get<std::shared_ptr<BTContext>>("context");
    return ctx->current_command == 7 ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
  }
};

class ManualMowBladeStartTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ctx_ = std::make_shared<BTContext>();
    ctx_->node = rclcpp::Node::make_shared("test_manual_mow_blade_start");
    ctx_->blade_auto_reverse = false;
    ctx_->current_command = 7;
    setBladeState(false, 0.0F);

    server_ = rclcpp::Node::make_shared("fake_manual_mow_hardware");
    service_ = server_->create_service<MowerControl>(
        "/hardware_bridge/mower_control",
        [this](const std::shared_ptr<MowerControl::Request> request,
               std::shared_ptr<MowerControl::Response> response)
        {
          requests_.push_back(*request);
          request_times_.push_back(std::chrono::steady_clock::now());
          response->success = true;
        });
    zero_sub_ = server_->create_subscription<geometry_msgs::msg::TwistStamped>(
        "/cmd_vel_emergency",
        10,
        [this](geometry_msgs::msg::TwistStamped::ConstSharedPtr message)
        {
          zero_commands_.push_back(*message);
          zero_command_times_.push_back(std::chrono::steady_clock::now());
        });
    high_level_status_sub_ = ctx_->node->create_subscription<HighLevelStatus>(
        "/test_manual_mow_blade_start/high_level_status",
        10,
        [this](HighLevelStatus::ConstSharedPtr message)
        {
          high_level_statuses_.push_back(*message);
        });

    blackboard_ = BT::Blackboard::create();
    blackboard_->set("context", ctx_);
    factory_.registerNodeType<ManualCommand>("ManualCommand");
    factory_.registerNodeType<ManualMowBladeStart>("ManualMowBladeStart");
    tree_ = std::make_unique<BT::Tree>(factory_.createTreeFromText(
        "<root BTCPP_format=\"4\"><BehaviorTree ID=\"Test\">"
        "<ReactiveSequence><ManualCommand/><ManualMowBladeStart/></ReactiveSequence>"
        "</BehaviorTree></root>",
        blackboard_));

    executor_.add_node(ctx_->node);
    executor_.add_node(server_);
    probe_ = ctx_->node->create_client<MowerControl>("/hardware_bridge/mower_control");
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!probe_->service_is_ready() && std::chrono::steady_clock::now() < deadline)
    {
      executor_.spin_some();
      std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(probe_->service_is_ready());
  }

  void setBladeState(bool active, float rpm)
  {
    std::lock_guard<std::mutex> lock(ctx_->context_mutex);
    auto& status = ctx_->latest_status;
    status.mower_status = Status::MOWER_STATUS_OK;
    status.firmware_compatible = true;
    status.mower_esc_status = active ? 1u : 0u;
    status.mower_motor_rpm = rpm;
    status.blade_status_stamp = ctx_->node->now().to_msg();
    ctx_->last_status_time = std::chrono::steady_clock::now();
  }

  void refreshStatusReceipt()
  {
    std::lock_guard<std::mutex> lock(ctx_->context_mutex);
    ctx_->last_status_time = std::chrono::steady_clock::now();
  }

  BT::NodeStatus step(bool fresh_off_sample = false)
  {
    if (fresh_off_sample)
      setBladeState(false, 0.0F);
    else
      refreshStatusReceipt();
    const auto result = tree_->tickOnce();
    executor_.spin_some();
    std::this_thread::sleep_for(20ms);
    executor_.spin_some();
    return result;
  }

  bool waitForRequestCount(std::size_t count,
                           bool update_off_sample,
                           std::chrono::milliseconds timeout)
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (requests_.size() < count && std::chrono::steady_clock::now() < deadline)
      step(update_off_sample);
    return requests_.size() >= count;
  }

  void spinFor(std::chrono::milliseconds duration, bool update_off_sample)
  {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
      step(update_off_sample);
  }

  void assertAllVelocityCommandsAreZero()
  {
    ASSERT_FALSE(zero_commands_.empty());
    for (const auto& message : zero_commands_)
    {
      EXPECT_DOUBLE_EQ(message.twist.linear.x, 0.0);
      EXPECT_DOUBLE_EQ(message.twist.linear.y, 0.0);
      EXPECT_DOUBLE_EQ(message.twist.linear.z, 0.0);
      EXPECT_DOUBLE_EQ(message.twist.angular.x, 0.0);
      EXPECT_DOUBLE_EQ(message.twist.angular.y, 0.0);
      EXPECT_DOUBLE_EQ(message.twist.angular.z, 0.0);
    }
  }

  void assertIdleToManualStatusOrder()
  {
    const auto idle = std::find_if(high_level_statuses_.begin(),
                                   high_level_statuses_.end(),
                                   [](const HighLevelStatus& status)
                                   {
                                     return status.state == HighLevelStatus::HIGH_LEVEL_STATE_IDLE;
                                   });
    const auto manual =
        std::find_if(high_level_statuses_.begin(),
                     high_level_statuses_.end(),
                     [](const HighLevelStatus& status)
                     {
                       return status.state == HighLevelStatus::HIGH_LEVEL_STATE_MANUAL_MOWING;
                     });
    ASSERT_NE(idle, high_level_statuses_.end()) << "arming must publish firmware IDLE";
    ASSERT_NE(manual, high_level_statuses_.end()) << "arming must restore MANUAL_MOWING";
    EXPECT_LT(idle, manual);
  }

  void assertZeroPreludeSpansOffToOn()
  {
    ASSERT_GE(request_times_.size(), 2u);
    std::vector<std::chrono::steady_clock::time_point> prelude_zeros;
    for (const auto& stamp : zero_command_times_)
    {
      if (stamp >= request_times_[0] && stamp <= request_times_[1])
        prelude_zeros.push_back(stamp);
    }
    ASSERT_GE(prelude_zeros.size(), 2u);
    EXPECT_LT(prelude_zeros.front() - request_times_[0], 100ms);
    EXPECT_GE(prelude_zeros.back() - prelude_zeros.front(), 650ms);
  }

  std::shared_ptr<BTContext> ctx_;
  BT::Blackboard::Ptr blackboard_;
  BT::BehaviorTreeFactory factory_;
  std::unique_ptr<BT::Tree> tree_;
  rclcpp::Node::SharedPtr server_;
  rclcpp::Service<MowerControl>::SharedPtr service_;
  rclcpp::Subscription<geometry_msgs::msg::TwistStamped>::SharedPtr zero_sub_;
  rclcpp::Subscription<HighLevelStatus>::SharedPtr high_level_status_sub_;
  rclcpp::Client<MowerControl>::SharedPtr probe_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::vector<MowerControl::Request> requests_;
  std::vector<std::chrono::steady_clock::time_point> request_times_;
  std::vector<geometry_msgs::msg::TwistStamped> zero_commands_;
  std::vector<std::chrono::steady_clock::time_point> zero_command_times_;
  std::vector<HighLevelStatus> high_level_statuses_;
};

}  // namespace

TEST_F(ManualMowBladeStartTest, WaitsForFreshOffThenRpmAndTurnsOffWhenPreempted)
{
  ASSERT_EQ(step(), BT::NodeStatus::RUNNING);
  ASSERT_TRUE(waitForRequestCount(1, false, 1s));
  EXPECT_EQ(requests_[0].mow_enabled, 0u);

  // Keep delivering fresh inactive firmware reports. ON must wait for both a
  // report newer than OFF acceptance and the complete zero-velocity prelude.
  spinFor(350ms, true);
  EXPECT_EQ(requests_.size(), 1u);
  ASSERT_TRUE(waitForRequestCount(2, true, 2s));
  EXPECT_EQ(requests_[1].mow_enabled, 1u);
  EXPECT_GE(request_times_[1] - request_times_[0], 700ms);
  assertIdleToManualStatusOrder();
  assertZeroPreludeSpansOffToOn();

  // Service acceptance is not proof of blade activity; only fresh RPM is.
  const auto zeros_before_rpm = zero_commands_.size();
  setBladeState(true, 3400.0F);
  EXPECT_EQ(step(), BT::NodeStatus::RUNNING);
  ASSERT_EQ(requests_.size(), 2u);
  spinFor(250ms, false);
  EXPECT_LE(zero_commands_.size() - zeros_before_rpm, 2u)
      << "the emergency zero stream should be released after RPM confirmation";

  // A ReactiveSequence command change halts the action and sends OFF.
  ctx_->current_command = 8;
  EXPECT_EQ(step(), BT::NodeStatus::FAILURE);
  ASSERT_TRUE(waitForRequestCount(3, false, 1s));
  EXPECT_EQ(requests_[2].mow_enabled, 0u);
  assertAllVelocityCommandsAreZero();
}

TEST_F(ManualMowBladeStartTest, ZeroRpmFeedbackTimeoutSendsOffAndDoesNotRetry)
{
  ASSERT_EQ(step(), BT::NodeStatus::RUNNING);
  ASSERT_TRUE(waitForRequestCount(1, false, 1s));
  spinFor(350ms, true);
  ASSERT_TRUE(waitForRequestCount(2, true, 2s));
  EXPECT_EQ(requests_[1].mow_enabled, 1u);

  // Refresh the bridge status receipt while deliberately withholding a newer
  // blade sample, matching a healthy host link with no RPM telemetry progress.
  spinFor(4300ms, false);
  ASSERT_TRUE(waitForRequestCount(3, false, 1s));
  EXPECT_EQ(requests_[2].mow_enabled, 0u);
  EXPECT_EQ(step(), BT::NodeStatus::RUNNING);
  spinFor(300ms, false);
  EXPECT_EQ(requests_.size(), 3u) << "failed manual start must not retry at BT rate";
  assertAllVelocityCommandsAreZero();
}

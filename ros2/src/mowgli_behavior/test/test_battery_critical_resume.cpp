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

// SPDX-License-Identifier: GPL-3.0
/**
 * @file test_battery_critical_resume.cpp
 * @brief Regression for "robot stuck after a critical-battery charge".
 *
 * The CriticalBatteryDock branch used to END the session unconditionally
 * (EndSession + ClearCommand) once it left the charge-hold, so after a
 * critical-battery event the robot docked, charged fully, and then sat idle
 * forever — current_command was cleared and the coverage resume cursor was
 * wiped. The fix makes recovery AUTO-CONTINUE: after charging to the resume
 * level it undocks and falls through WITHOUT EndSession/ClearCommand (so
 * MowingSequence resumes from the saved cursor), and it ONLY ends the session
 * on a dead charger (CriticalChargerFailed), aborting the branch with FAILURE
 * so the undock/resume tail is skipped.
 *
 * These tests exercise the exact control flow of the tail of CriticalBatteryDock
 * (from the CriticalChargeOrAbort Fallback onward) using the real EndSession,
 * ClearCommand, IsBatteryAbove and IsResumeUndockAllowed nodes, with stand-ins
 * for IsChargingProgressing (controllable) and BackUp (a marker that records
 * whether the undock/resume tail ran). The IsBatteryLow entry gate is unchanged
 * by the fix and is omitted here (it cannot coexist with the resume gate in a
 * single tick — entry needs battery < 10 %, resume needs battery >= 95 %).
 */

#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "behaviortree_cpp/bt_factory.h"
#include "mowgli_behavior/bt_context.hpp"
#include "mowgli_behavior/condition_nodes.hpp"
#include "mowgli_behavior/status_nodes.hpp"
#include <gtest/gtest.h>

using mowgli_behavior::BTContext;
using mowgli_behavior::ClearCommand;
using mowgli_behavior::EndSession;
using mowgli_behavior::IsBatteryAbove;
using mowgli_behavior::IsCommand;
using mowgli_behavior::IsResumeUndockAllowed;

// ---------------------------------------------------------------------------
// Global ROS2 init/shutdown
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
public:
  void SetUp() override
  {
    if (!rclcpp::ok())
    {
      rclcpp::init(0, nullptr);
    }
  }
  void TearDown() override
  {
    rclcpp::shutdown();
  }
};

::testing::Environment* const rclcpp_env =
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment());

// BehaviorTree.CPP rejects RUNNING from a SyncActionNode. This stateful
// surrogate stays RUNNING until the ReactiveSequence halts it on STOP.
class WaitForCharge : public BT::StatefulActionNode
{
public:
  WaitForCharge(const std::string& name, const BT::NodeConfig& config)
      : BT::StatefulActionNode(name, config)
  {
  }

  static BT::PortsList providedPorts()
  {
    return {};
  }

  BT::NodeStatus onStart() override
  {
    return BT::NodeStatus::RUNNING;
  }

  BT::NodeStatus onRunning() override
  {
    return BT::NodeStatus::RUNNING;
  }

  void onHalted() override
  {
  }
};

// ---------------------------------------------------------------------------
// Fixture — mirrors the tail of CriticalBatteryDock (charge-hold + branch).
//
// A returned SUCCESS means the recovery/undock tail ran; FAILURE means the
// charger-failed abort ran (undock tail skipped). ChargingProgress and
// UndockMarker are stand-ins we control / observe; every other node is real.
// ---------------------------------------------------------------------------

class CriticalBatteryResumeTest : public ::testing::Test
{
protected:
  std::shared_ptr<BTContext> ctx;
  BT::Blackboard::Ptr blackboard;
  BT::BehaviorTreeFactory factory;

  bool charging_ok = true;  // stand-in for IsChargingProgressing
  int undock_count = 0;  // stand-in for BackUp (undock/resume tail)

  void SetUp() override
  {
    ctx = std::make_shared<BTContext>();
    ctx->node = rclcpp::Node::make_shared("test_battery_critical_resume");

    blackboard = BT::Blackboard::create();
    blackboard->set("context", ctx);
    // Resume level pulled by {battery_full_pct} — the same knob MowingSequence
    // uses; matches the mowgli_robot.yaml default.
    blackboard->set("battery_full_pct", 95.0f);

    factory.registerNodeType<IsBatteryAbove>("IsBatteryAbove");
    factory.registerNodeType<IsCommand>("IsCommand");
    factory.registerNodeType<IsResumeUndockAllowed>("IsResumeUndockAllowed");
    factory.registerNodeType<EndSession>("EndSession");
    factory.registerNodeType<ClearCommand>("ClearCommand");

    factory.registerSimpleCondition("ChargingProgress",
                                    [this](BT::TreeNode&)
                                    {
                                      return charging_ok ? BT::NodeStatus::SUCCESS
                                                         : BT::NodeStatus::FAILURE;
                                    });
    factory.registerSimpleAction("UndockMarker",
                                 [this](BT::TreeNode&)
                                 {
                                   ++undock_count;
                                   return BT::NodeStatus::SUCCESS;
                                 });
    factory.registerNodeType<WaitForCharge>("WaitForCharge");
  }

  BT::Tree makeTree()
  {
    // The inner 5 s wait is a controllable RUNNING stand-in: recovery
    // short-circuits it, a dead charger fails before it, and STOP must halt it
    // immediately through the surrounding ReactiveSequence.
    static const char* xml = R"(
      <root BTCPP_format="4">
        <BehaviorTree ID="MainTree">
          <Sequence name="CriticalBatteryDockTail">
            <ReactiveSequence name="CriticalChargeHold">
              <Inverter name="CriticalChargeHoldNotStopped"><IsCommand command="8"/></Inverter>
              <Fallback name="CriticalChargeOrAbort">
              <RetryUntilSuccessful num_attempts="960">
                <Sequence>
                  <ChargingProgress/>
                  <Fallback>
                    <IsBatteryAbove threshold="{battery_full_pct}"/>
                    <WaitForCharge/>
                  </Fallback>
                </Sequence>
              </RetryUntilSuccessful>
              <Sequence name="CriticalChargerFailed">
                <EndSession/>
                <ClearCommand/>
                <AlwaysFailure/>
              </Sequence>
              </Fallback>
            </ReactiveSequence>
            <IsResumeUndockAllowed max_attempts="3"/>
            <UndockMarker/>
          </Sequence>
        </BehaviorTree>
      </root>
    )";
    return factory.createTreeFromText(xml, blackboard);
  }
};

// Recovery: charged past battery_full_pct with a healthy charger MUST
// auto-continue — undock and fall through WITHOUT clearing the command or the
// resume cursor, so MowingSequence resumes from where it left off.
TEST_F(CriticalBatteryResumeTest, RecoveryAutoContinuesWithoutEndingSession)
{
  ctx->current_command = 1;  // COMMAND_START in flight
  ctx->area_resume_pose_index[0] = 42;  // saved coverage cursor
  ctx->battery_percent = 100.0f;  // fully charged
  charging_ok = true;

  auto tree = makeTree();
  EXPECT_EQ(tree.tickOnce(), BT::NodeStatus::SUCCESS);

  // The whole point of the fix: neither EndSession nor ClearCommand ran.
  EXPECT_EQ(ctx->current_command, 1);
  ASSERT_EQ(ctx->area_resume_pose_index.count(0), 1u);
  EXPECT_EQ(ctx->area_resume_pose_index[0], 42u);
  // The undock/resume tail actually executed.
  EXPECT_EQ(undock_count, 1);
}

// An operator cancel during the *completed* critical charge hold must leave
// the loop immediately. It is a normal STOP hold, not a dead-charger session
// end: preserve the cursor and never run the undock/resume tail.
TEST_F(CriticalBatteryResumeTest, StopDuringChargeHoldPreservesSessionAndSkipsUndock)
{
  ctx->current_command = 1;  // COMMAND_START; already waiting on the dock
  ctx->area_resume_pose_index[0] = 42;
  ctx->battery_percent = 50.0f;
  charging_ok = true;

  auto tree = makeTree();
  EXPECT_EQ(tree.tickOnce(), BT::NodeStatus::RUNNING);
  ctx->current_command = 8;  // COMMAND_STOP arrives while WaitForCharge runs
  EXPECT_EQ(tree.tickOnce(), BT::NodeStatus::FAILURE);
  EXPECT_EQ(ctx->current_command, 8);
  ASSERT_EQ(ctx->area_resume_pose_index.count(0), 1u);
  EXPECT_EQ(ctx->area_resume_pose_index[0], 42u);
  EXPECT_EQ(undock_count, 0);
}

TEST(CriticalBatteryResumeStructureTest, StopGatesArePostDockReactiveHoldsBeforeBothWaits)
{
  std::ifstream input(MOWGLI_MAIN_TREE_PATH);
  ASSERT_TRUE(input.good());
  std::ostringstream contents;
  contents << input.rdbuf();
  const std::string tree = contents.str();
  const auto critical = tree.find("<Sequence name=\"CriticalBatteryDock\">");
  const auto dock = tree.find("<DockRobot", critical);
  const auto reactive_hold = tree.find("<ReactiveSequence name=\"CriticalChargeHold\">", critical);
  const auto stop_gate = tree.find("CriticalChargeHoldNotStopped", critical);
  const auto wait = tree.find("<Fallback name=\"CriticalChargeOrAbort\">", critical);
  const auto undock = tree.find("<IsResumeUndockAllowed", critical);
  ASSERT_NE(critical, std::string::npos);
  ASSERT_NE(dock, std::string::npos);
  ASSERT_NE(reactive_hold, std::string::npos);
  ASSERT_NE(stop_gate, std::string::npos);
  ASSERT_NE(wait, std::string::npos);
  ASSERT_NE(undock, std::string::npos);
  EXPECT_LT(dock, stop_gate);
  EXPECT_LT(reactive_hold, stop_gate);
  EXPECT_LT(stop_gate, wait);
  EXPECT_LT(wait, undock);

  const auto low_dock = tree.find("<Sequence name=\"BatteryDockAndResume\">");
  const auto low_dock_action = tree.find("<DockRobot", low_dock);
  const auto low_reactive_hold = tree.find("<ReactiveSequence name=\"ChargeHold\">", low_dock);
  const auto low_stop_gate = tree.find("ChargeHoldNotStopped", low_dock);
  const auto low_wait = tree.find("<Fallback name=\"ChargeOrAbort\">", low_dock);
  const auto low_undock = tree.find("<IsResumeUndockAllowed", low_dock);
  ASSERT_NE(low_dock, std::string::npos);
  ASSERT_NE(low_dock_action, std::string::npos);
  ASSERT_NE(low_reactive_hold, std::string::npos);
  ASSERT_NE(low_stop_gate, std::string::npos);
  ASSERT_NE(low_wait, std::string::npos);
  ASSERT_NE(low_undock, std::string::npos);
  EXPECT_LT(low_dock_action, low_reactive_hold);
  EXPECT_LT(low_reactive_hold, low_stop_gate);
  EXPECT_LT(low_stop_gate, low_wait);
  EXPECT_LT(low_wait, low_undock);
}

// Dead charger: no charge progress MUST end the session (EndSession +
// ClearCommand) and abort the branch with FAILURE so the undock tail is
// SKIPPED — never resume mowing on a critical pack.
TEST_F(CriticalBatteryResumeTest, DeadChargerEndsSessionAndSkipsUndock)
{
  ctx->current_command = 1;
  ctx->area_resume_pose_index[0] = 42;
  ctx->battery_percent = 50.0f;  // charge stalled below resume level
  charging_ok = false;  // charger not progressing

  auto tree = makeTree();
  EXPECT_EQ(tree.tickOnce(), BT::NodeStatus::FAILURE);

  // Session ended: command cleared and resume cursor wiped.
  EXPECT_EQ(ctx->current_command, 0);
  EXPECT_TRUE(ctx->area_resume_pose_index.empty());
  // The undock/resume tail must NOT have run.
  EXPECT_EQ(undock_count, 0);
}

// The resume-undock attempt cap still gates the auto-continue: once the session
// has exhausted its resume-undock budget, the tail fails (IsResumeUndockAllowed
// FAILURE) instead of undocking again — but the session/command are preserved
// (EndSession did not run on this healthy-charger path).
TEST_F(CriticalBatteryResumeTest, ResumeUndockCapBlocksUndockButKeepsSession)
{
  ctx->current_command = 1;
  ctx->area_resume_pose_index[0] = 42;
  ctx->battery_percent = 100.0f;
  ctx->resume_undock_failures = 3;  // budget exhausted (max_attempts=3)
  charging_ok = true;

  auto tree = makeTree();
  EXPECT_EQ(tree.tickOnce(), BT::NodeStatus::FAILURE);

  // Charger was healthy → EndSession never ran → command + cursor survive.
  EXPECT_EQ(ctx->current_command, 1);
  ASSERT_EQ(ctx->area_resume_pose_index.count(0), 1u);
  EXPECT_EQ(undock_count, 0);
}

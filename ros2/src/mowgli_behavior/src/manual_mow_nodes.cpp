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

#include "mowgli_behavior/manual_mow_nodes.hpp"

#include "mowgli_behavior/status_snapshot.hpp"

namespace mowgli_behavior
{

namespace
{

constexpr auto kPollInterval = std::chrono::milliseconds(0);

}  // namespace

void ManualMowBladeStart::initializeIo(const std::shared_ptr<BTContext>& ctx)
{
  if (!zero_publisher_)
  {
    zero_publisher_ =
        ctx->node->create_publisher<geometry_msgs::msg::TwistStamped>("/cmd_vel_emergency", 10);
  }
  if (!blade_client_)
  {
    blade_client_ = ctx->bladeClient();
  }
}

void ManualMowBladeStart::publishHighLevelStatus(const std::shared_ptr<BTContext>& ctx,
                                                 uint8_t state,
                                                 const std::string& state_name)
{
  std::lock_guard<std::mutex> lock(ctx->context_mutex);
  if (!ctx->high_level_status_pub)
  {
    ctx->high_level_status_pub =
        ctx->node->create_publisher<mowgli_interfaces::msg::HighLevelStatus>("~/high_level_status",
                                                                             10);
  }

  mowgli_interfaces::msg::HighLevelStatus identity;
  identity.state = state;
  identity.state_name = state_name;
  ctx->last_high_level_status = withLiveStatusFields(identity, *ctx);
  ctx->has_high_level_status = true;
  ctx->high_level_status_pub->publish(ctx->last_high_level_status);
}

void ManualMowBladeStart::publishZero(const std::shared_ptr<BTContext>& ctx)
{
  geometry_msgs::msg::TwistStamped zero{};
  zero.header.stamp = ctx->node->now();
  zero.header.frame_id = "base_footprint";
  zero_publisher_->publish(zero);
}

ManualMowBladeStart::StatusSnapshot ManualMowBladeStart::statusSnapshot(
    const std::shared_ptr<BTContext>& ctx) const
{
  StatusSnapshot snapshot;
  std::lock_guard<std::mutex> lock(ctx->context_mutex);
  snapshot.status = ctx->latest_status;
  snapshot.received_at = ctx->last_status_time;
  return snapshot;
}

int64_t ManualMowBladeStart::bladeStampNs(const mowgli_interfaces::msg::Status& status)
{
  return static_cast<int64_t>(status.blade_status_stamp.sec) * 1000000000LL +
         static_cast<int64_t>(status.blade_status_stamp.nanosec);
}

bool ManualMowBladeStart::hasFreshCompatibleStatus(const StatusSnapshot& snapshot) const
{
  const auto now = std::chrono::steady_clock::now();
  if (snapshot.received_at.time_since_epoch().count() == 0 || now < snapshot.received_at ||
      now - snapshot.received_at > kStatusMaxAge)
  {
    return false;
  }

  return snapshot.status.firmware_compatible &&
         snapshot.status.mower_status == mowgli_interfaces::msg::Status::MOWER_STATUS_OK;
}

bool ManualMowBladeStart::hasFreshBladeSample(const StatusSnapshot& snapshot,
                                              const std::shared_ptr<BTContext>& ctx) const
{
  const int64_t stamp_ns = bladeStampNs(snapshot.status);
  if (stamp_ns <= 0)
  {
    return false;
  }

  const rclcpp::Time sample_time(snapshot.status.blade_status_stamp,
                                 ctx->node->get_clock()->get_clock_type());
  const auto age = ctx->node->now() - sample_time;
  return age.nanoseconds() >= 0 &&
         age.seconds() <= std::chrono::duration<double>(kBladeSampleMaxAge).count();
}

bool ManualMowBladeStart::sendBladeRequest(const std::shared_ptr<BTContext>& ctx, bool enabled)
{
  auto command = ctx->blade_direction.forMowerCommand(enabled, ctx->blade_auto_reverse);
  if (enabled && command.enabled == 0u)
  {
    return false;
  }

  auto request = std::make_shared<MowerControl::Request>();
  request->mow_enabled = command.enabled;
  request->mow_direction = command.direction;
  pending_request_.emplace(blade_client_->async_send_request(request));
  phase_started_at_ = std::chrono::steady_clock::now();
  return true;
}

void ManualMowBladeStart::sendBestEffortOff(const std::shared_ptr<BTContext>& ctx)
{
  removePendingRequest();
  auto command = ctx->blade_direction.forMowerCommand(false, ctx->blade_auto_reverse);
  auto request = std::make_shared<MowerControl::Request>();
  request->mow_enabled = command.enabled;
  request->mow_direction = command.direction;
  (void)blade_client_->async_send_request(request);
}

void ManualMowBladeStart::removePendingRequest()
{
  if (pending_request_)
  {
    blade_client_->remove_pending_request(*pending_request_);
    pending_request_.reset();
  }
}

void ManualMowBladeStart::enterFault(const std::shared_ptr<BTContext>& ctx,
                                     const std::string& reason)
{
  if (phase_ == Phase::Fault)
  {
    return;
  }

  phase_ = Phase::Fault;
  phase_started_at_ = std::chrono::steady_clock::now();
  publishZero(ctx);
  sendBestEffortOff(ctx);
  RCLCPP_ERROR(ctx->node->get_logger(),
               "ManualMowBladeStart: %s; requesting blade OFF and holding zero velocity until "
               "manual-mow command changes",
               reason.c_str());
}

BT::NodeStatus ManualMowBladeStart::onStart()
{
  auto ctx = config().blackboard->get<std::shared_ptr<BTContext>>("context");
  initializeIo(ctx);
  phase_ = Phase::Idle;
  pending_request_.reset();
  off_response_stamp_ns_ = 0;
  on_request_stamp_ns_ = 0;
  publishZero(ctx);

  const auto snapshot = statusSnapshot(ctx);
  if (!hasFreshCompatibleStatus(snapshot))
  {
    enterFault(ctx, "fresh compatible hardware status is unavailable");
    return BT::NodeStatus::RUNNING;
  }

  if (!sendBladeRequest(ctx, false))
  {
    enterFault(ctx, "could not request blade OFF before re-arm");
    return BT::NodeStatus::RUNNING;
  }

  // The firmware only counts an exact-zero re-arm phase while its mirrored
  // mode is IDLE. Publish IDLE after requesting OFF; the zero stream below is
  // held long enough for the host bridge and firmware to process that change.
  publishHighLevelStatus(ctx,
                         mowgli_interfaces::msg::HighLevelStatus::HIGH_LEVEL_STATE_IDLE,
                         "MANUAL_MOW_REARM");

  off_response_stamp_ns_ = bladeStampNs(snapshot.status);
  phase_ = Phase::WaitingForOffResponse;
  RCLCPP_INFO(ctx->node->get_logger(),
              "ManualMowBladeStart: requested blade OFF and IDLE before zero-velocity re-arm");
  return BT::NodeStatus::RUNNING;
}

BT::NodeStatus ManualMowBladeStart::onRunning()
{
  auto ctx = config().blackboard->get<std::shared_ptr<BTContext>>("context");
  if (phase_ != Phase::Active)
  {
    publishZero(ctx);
  }

  if (phase_ == Phase::Fault)
  {
    return BT::NodeStatus::RUNNING;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto snapshot = statusSnapshot(ctx);

  if (phase_ != Phase::WaitingForOffResponse && !hasFreshCompatibleStatus(snapshot))
  {
    enterFault(ctx, "hardware status became stale or incompatible during blade start");
    return BT::NodeStatus::RUNNING;
  }

  switch (phase_)
  {
    case Phase::WaitingForOffResponse:
      if (pending_request_->future.wait_for(kPollInterval) == std::future_status::ready)
      {
        const auto response = pending_request_->future.get();
        pending_request_.reset();
        if (!response || !response->success)
        {
          enterFault(ctx, "hardware bridge did not accept blade OFF");
          break;
        }
        if (!hasFreshCompatibleStatus(snapshot))
        {
          enterFault(ctx, "hardware status went stale while waiting for blade OFF response");
          break;
        }

        // The bridge response confirms only host acceptance. Require a newer
        // firmware blade sample after that response before allowing blade ON.
        off_response_stamp_ns_ = bladeStampNs(snapshot.status);
        rearm_started_at_ = now;
        phase_ = Phase::WaitingForOffTelemetry;
      }
      else if (now - phase_started_at_ >= kServiceTimeout)
      {
        enterFault(ctx, "timed out waiting for blade OFF service response");
      }
      break;

    case Phase::WaitingForOffTelemetry:
    {
      const bool newer_off_sample = hasFreshBladeSample(snapshot, ctx) &&
                                    bladeStampNs(snapshot.status) > off_response_stamp_ns_ &&
                                    snapshot.status.mower_esc_status == 0u &&
                                    snapshot.status.mower_motor_rpm == 0.0F;
      if (now - rearm_started_at_ >= kOffTelemetryTimeout)
      {
        enterFault(ctx, "no newer fresh inactive/0-RPM blade sample after OFF");
        break;
      }

      if (newer_off_sample && now - rearm_started_at_ >= kMinimumRearmPrelude)
      {
        publishHighLevelStatus(
            ctx,
            mowgli_interfaces::msg::HighLevelStatus::HIGH_LEVEL_STATE_MANUAL_MOWING,
            "MANUAL_MOWING");
        mode_published_at_ = now;
        phase_ = Phase::WaitingForModePropagation;
        RCLCPP_INFO(ctx->node->get_logger(),
                    "ManualMowBladeStart: OFF telemetry confirmed after %.2fs of exact-zero "
                    "velocity; publishing MANUAL_MOWING",
                    std::chrono::duration<double>(now - rearm_started_at_).count());
      }
      break;
    }

    case Phase::WaitingForModePropagation:
      if (now - mode_published_at_ >= kModePropagationDelay)
      {
        on_request_stamp_ns_ = bladeStampNs(snapshot.status);
        if (!sendBladeRequest(ctx, true))
        {
          enterFault(ctx, "blade ON request is inhibited by the active blade policy");
          break;
        }
        phase_ = Phase::WaitingForOnResponse;
        RCLCPP_INFO(ctx->node->get_logger(),
                    "ManualMowBladeStart: requested blade ON after MANUAL_MOWING propagation");
      }
      break;

    case Phase::WaitingForOnResponse:
      if (pending_request_->future.wait_for(kPollInterval) == std::future_status::ready)
      {
        const auto response = pending_request_->future.get();
        pending_request_.reset();
        if (!response || !response->success)
        {
          enterFault(ctx, "hardware bridge did not accept blade ON");
          break;
        }
        phase_started_at_ = now;
        phase_ = Phase::WaitingForActiveTelemetry;
      }
      else if (now - phase_started_at_ >= kServiceTimeout)
      {
        enterFault(ctx, "timed out waiting for blade ON service response");
      }
      break;

    case Phase::WaitingForActiveTelemetry:
      if (now - phase_started_at_ >= kActiveTelemetryTimeout)
      {
        enterFault(ctx, "no newer fresh active blade sample with RPM above zero after ON");
        break;
      }
      if (hasFreshBladeSample(snapshot, ctx) &&
          bladeStampNs(snapshot.status) > on_request_stamp_ns_ &&
          snapshot.status.mower_esc_status != 0u && snapshot.status.mower_motor_rpm > 0.0F)
      {
        phase_ = Phase::Active;
        RCLCPP_INFO(ctx->node->get_logger(),
                    "ManualMowBladeStart: firmware reports active blade telemetry at %.0f RPM",
                    snapshot.status.mower_motor_rpm);
      }
      break;

    case Phase::Active:
      if (!hasFreshBladeSample(snapshot, ctx) || snapshot.status.mower_esc_status == 0u ||
          snapshot.status.mower_motor_rpm <= 0.0F)
      {
        enterFault(ctx, "fresh active blade RPM telemetry was lost during manual mowing");
      }
      break;

    case Phase::Idle:
    case Phase::Fault:
      break;
  }

  return BT::NodeStatus::RUNNING;
}

void ManualMowBladeStart::onHalted()
{
  auto ctx = config().blackboard->get<std::shared_ptr<BTContext>>("context");
  if (ctx && ctx->node)
  {
    initializeIo(ctx);
    publishZero(ctx);
    sendBestEffortOff(ctx);
  }
  phase_ = Phase::Idle;
}

}  // namespace mowgli_behavior

#!/bin/bash
# SPDX-License-Identifier: GPL-3.0
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
script="${repo_root}/sensors/gps/start_gps.sh"
temp_dir="$(mktemp -d)"
trap 'rm -rf "$temp_dir"' EXIT

mkdir -p "$temp_dir/config"
printf '# mock ROS setup\n' >"$temp_dir/ros_setup.bash"
printf '# mock GNSS overlay\n' >"$temp_dir/gnss_setup.bash"
touch "$temp_dir/serial"

run_dry() {
  GNSS_DRY_RUN=true \
  GNSS_CONFIG_PATH="$temp_dir/config/mowgli_robot.yaml" \
  ROS_SETUP_BASH="$temp_dir/ros_setup.bash" \
  GNSS_SIDECAR_SETUP_BASH="$temp_dir/gnss_setup.bash" \
  GNSS_SERIAL_DEVICE="$temp_dir/serial" \
  bash "$script"
}

cat >"$temp_dir/config/mowgli_robot.yaml" <<'EOF'
  gnss_correction_source: tcp
  gnss_rtcm_tcp_host: 127.0.0.1
  gnss_rtcm_tcp_port: 5015
EOF
tcp_output="$(run_dry)"
grep -Fq 'rtcm_tcp_source' <<<"$tcp_output"
! grep -Fq 'ntrip_node' <<<"$tcp_output"

cat >"$temp_dir/config/mowgli_robot.yaml" <<'EOF'
  gnss_correction_source: none
EOF
none_output="$(run_dry)"
! grep -Fq 'rtcm_tcp_source' <<<"$none_output"
! grep -Fq 'ntrip_node' <<<"$none_output"

cat >"$temp_dir/config/mowgli_robot.yaml" <<'EOF'
  gnss_correction_source: invalid
EOF
if run_dry >/dev/null 2>&1; then
  echo "invalid correction source unexpectedly succeeded" >&2
  exit 1
fi

cat >"$temp_dir/config/mowgli_robot.yaml" <<'EOF'
  gnss_correction_source: tcp
  gnss_rtcm_tcp_host: 127.0.0.1
  gnss_rtcm_tcp_port: 0
EOF
if run_dry >/dev/null 2>&1; then
  echo "invalid TCP endpoint unexpectedly succeeded" >&2
  exit 1
fi

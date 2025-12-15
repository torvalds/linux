#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Test YNL ethtool functionality

# Load KTAP test helpers
KSELFTEST_KTAP_HELPERS="$(dirname "$(realpath "$0")")/../../../testing/selftests/kselftest/ktap_helpers.sh"
# shellcheck source=../../../testing/selftests/kselftest/ktap_helpers.sh
source "$KSELFTEST_KTAP_HELPERS"

# Default ynl-ethtool path for direct execution, can be overridden by make install
ynl_ethtool="../pyynl/ethtool.py"

readonly NSIM_ID="1337"
readonly NSIM_DEV_NAME="nsim${NSIM_ID}"
readonly VETH_A="veth_a"
readonly VETH_B="veth_b"

testns="ynl-ethtool-$(mktemp -u XXXXXX)"
TESTS_NO=0

# Uses veth device as netdevsim doesn't support basic ethtool device info
ethtool_device_info()
{
	local info_output

	info_output=$(ip netns exec "$testns" $ynl_ethtool "$VETH_A" 2>/dev/null)

	if ! echo "$info_output" | grep -q "Settings for"; then
		ktap_test_fail "YNL ethtool device info (device info output missing expected content)"
		return
	fi

	ktap_test_pass "YNL ethtool device info"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_statistics()
{
	local stats_output

	stats_output=$(ip netns exec "$testns" $ynl_ethtool --statistics "$NSIM_DEV_NAME" 2>/dev/null)

	if ! echo "$stats_output" | grep -q -E "(NIC statistics|packets|bytes)"; then
		ktap_test_fail "YNL ethtool statistics (statistics output missing expected content)"
		return
	fi

	ktap_test_pass "YNL ethtool statistics"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_ring_params()
{
	local ring_output

	ring_output=$(ip netns exec "$testns" $ynl_ethtool --show-ring "$NSIM_DEV_NAME" 2>/dev/null)

	if ! echo "$ring_output" | grep -q -E "(Ring parameters|RX|TX)"; then
		ktap_test_fail "YNL ethtool ring parameters (ring parameters output missing expected content)"
		return
	fi

	if ! ip netns exec "$testns" $ynl_ethtool --set-ring "$NSIM_DEV_NAME" rx 64 2>/dev/null; then
		ktap_test_fail "YNL ethtool ring parameters (set-ring command failed unexpectedly)"
		return
	fi

	ktap_test_pass "YNL ethtool ring parameters (show/set)"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_coalesce_params()
{
	if ! ip netns exec "$testns" $ynl_ethtool --show-coalesce "$NSIM_DEV_NAME" &>/dev/null; then
		ktap_test_fail "YNL ethtool coalesce parameters (failed to get coalesce parameters)"
		return
	fi

	if ! ip netns exec "$testns" $ynl_ethtool --set-coalesce "$NSIM_DEV_NAME" rx-usecs 50 2>/dev/null; then
		ktap_test_fail "YNL ethtool coalesce parameters (set-coalesce command failed unexpectedly)"
		return
	fi

	ktap_test_pass "YNL ethtool coalesce parameters (show/set)"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_pause_params()
{
	if ! ip netns exec "$testns" $ynl_ethtool --show-pause "$NSIM_DEV_NAME" &>/dev/null; then
		ktap_test_fail "YNL ethtool pause parameters (failed to get pause parameters)"
		return
	fi

	if ! ip netns exec "$testns" $ynl_ethtool --set-pause "$NSIM_DEV_NAME" tx 1 rx 1 2>/dev/null; then
		ktap_test_fail "YNL ethtool pause parameters (set-pause command failed unexpectedly)"
		return
	fi

	ktap_test_pass "YNL ethtool pause parameters (show/set)"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_features_info()
{
	local features_output

	features_output=$(ip netns exec "$testns" $ynl_ethtool --show-features "$NSIM_DEV_NAME" 2>/dev/null)

	if ! echo "$features_output" | grep -q -E "(Features|offload)"; then
		ktap_test_fail "YNL ethtool features info (features output missing expected content)"
		return
	fi

	ktap_test_pass "YNL ethtool features info (show/set)"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_channels_info()
{
	local channels_output

	channels_output=$(ip netns exec "$testns" $ynl_ethtool --show-channels "$NSIM_DEV_NAME" 2>/dev/null)

	if ! echo "$channels_output" | grep -q -E "(Channel|Combined|RX|TX)"; then
		ktap_test_fail "YNL ethtool channels info (channels output missing expected content)"
		return
	fi

	if ! ip netns exec "$testns" $ynl_ethtool --set-channels "$NSIM_DEV_NAME" combined-count 1 2>/dev/null; then
		ktap_test_fail "YNL ethtool channels info (set-channels command failed unexpectedly)"
		return
	fi

	ktap_test_pass "YNL ethtool channels info (show/set)"
}
TESTS_NO=$((TESTS_NO + 1))

ethtool_time_stamping()
{
	local ts_output

	ts_output=$(ip netns exec "$testns" $ynl_ethtool --show-time-stamping "$NSIM_DEV_NAME" 2>/dev/null)

	if ! echo "$ts_output" | grep -q -E "(Time stamping|timestamping|SOF_TIMESTAMPING)"; then
		ktap_test_fail "YNL ethtool time stamping (time stamping output missing expected content)"
		return
	fi

	ktap_test_pass "YNL ethtool time stamping"
}
TESTS_NO=$((TESTS_NO + 1))

setup()
{
	modprobe netdevsim &> /dev/null
	if ! [ -f /sys/bus/netdevsim/new_device ]; then
		ktap_skip_all "netdevsim module not available"
		exit "$KSFT_SKIP"
	fi

	if ! ip netns add "$testns" 2>/dev/null; then
		ktap_skip_all "failed to create test namespace"
		exit "$KSFT_SKIP"
	fi

	echo "$NSIM_ID 1" | ip netns exec "$testns" tee /sys/bus/netdevsim/new_device >/dev/null 2>&1 || {
		ktap_skip_all "failed to create netdevsim device"
		exit "$KSFT_SKIP"
	}

	local dev
	dev=$(ip netns exec "$testns" ls /sys/bus/netdevsim/devices/netdevsim$NSIM_ID/net 2>/dev/null | head -1)
	if [[ -z "$dev" ]]; then
		ktap_skip_all "failed to find netdevsim device"
		exit "$KSFT_SKIP"
	fi

	ip -netns "$testns" link set dev "$dev" name "$NSIM_DEV_NAME" 2>/dev/null || {
		ktap_skip_all "failed to rename netdevsim device"
		exit "$KSFT_SKIP"
	}

	ip -netns "$testns" link set dev "$NSIM_DEV_NAME" up 2>/dev/null

	if ! ip -n "$testns" link add "$VETH_A" type veth peer name "$VETH_B" 2>/dev/null; then
		ktap_skip_all "failed to create veth pair"
		exit "$KSFT_SKIP"
	fi

	ip -n "$testns" link set "$VETH_A" up 2>/dev/null
	ip -n "$testns" link set "$VETH_B" up 2>/dev/null
}

cleanup()
{
	ip netns exec "$testns" bash -c "echo $NSIM_ID > /sys/bus/netdevsim/del_device" 2>/dev/null || true
	ip netns del "$testns" 2>/dev/null || true
}

# Check if ynl-ethtool command is available
if ! command -v $ynl_ethtool &>/dev/null && [[ ! -x $ynl_ethtool ]]; then
	ktap_skip_all "ynl-ethtool command not found: $ynl_ethtool"
	exit "$KSFT_SKIP"
fi

trap cleanup EXIT

ktap_print_header
setup
ktap_set_plan "${TESTS_NO}"

ethtool_device_info
ethtool_statistics
ethtool_ring_params
ethtool_coalesce_params
ethtool_pause_params
ethtool_features_info
ethtool_channels_info
ethtool_time_stamping

ktap_finished

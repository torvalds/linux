#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

. "$(cd "$(dirname "$0")" && pwd)"/test_common.sh

ERR_CODE=0

_test_partition_scan_no_hang()
{
	local recovery_flag=$1
	local expected_state=$2
	local dev_id
	local state
	local daemon_pid
	local start_time
	local elapsed

	# Create ublk device with fault_inject target and very large delay
	# to simulate hang during partition table read
	# --delay_us 60000000 = 60 seconds delay
	# Use _add_ublk_dev_no_settle to avoid udevadm settle hang waiting
	# for partition scan events to complete
	if [ "$recovery_flag" = "yes" ]; then
		echo "Testing partition scan with recovery support..."
		dev_id=$(_add_ublk_dev_no_settle -t fault_inject -q 1 -d 1 --delay_us 60000000 -r 1)
	else
		echo "Testing partition scan without recovery..."
		dev_id=$(_add_ublk_dev_no_settle -t fault_inject -q 1 -d 1 --delay_us 60000000)
	fi

	_check_add_dev "$TID" $?

	# The add command should return quickly because partition scan is async.
	# Now sleep briefly to let the async partition scan work start and hit
	# the delay in the fault_inject handler.
	_ublk_sleep 1 5

	# Kill the ublk daemon while partition scan is potentially blocked
	# And check state transitions properly
	start_time=${SECONDS}
	daemon_pid=$(_get_ublk_daemon_pid "${dev_id}")
	state=$(__ublk_kill_daemon "${dev_id}" "${expected_state}")
	elapsed=$((SECONDS - start_time))

	# Verify the device transitioned to expected state
	if [ "$state" != "${expected_state}" ]; then
		echo "FAIL: Device state is $state, expected ${expected_state}"
		ERR_CODE=255
		_ublk_del_dev "${dev_id}" > /dev/null 2>&1
		return
	fi
	echo "PASS: Device transitioned to ${expected_state} in ${elapsed}s without hanging"

	# Clean up the device
	_ublk_del_dev "${dev_id}" > /dev/null 2>&1
}

_prep_test "partition_scan" "verify async partition scan prevents IO hang"

# Test 1: Without recovery support - should transition to DEAD
_test_partition_scan_no_hang "no" "DEAD"

# Test 2: With recovery support - should transition to QUIESCED
_test_partition_scan_no_hang "yes" "QUIESCED"

_cleanup_test "partition_scan"
_show_result $TID $ERR_CODE

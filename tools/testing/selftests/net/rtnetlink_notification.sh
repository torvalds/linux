#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking rtnetlink notification callpaths, and get as much
# coverage as possible.
#
# set -e

ALL_TESTS="
	kci_test_mcast_addr_notification
"

source lib.sh

kci_test_mcast_addr_notification()
{
	RET=0
	local tmpfile
	local monitor_pid
	local match_result
	local test_dev="test-dummy1"

	tmpfile=$(mktemp)
	defer rm "$tmpfile"

	ip monitor maddr > $tmpfile &
	monitor_pid=$!
	defer kill_process "$monitor_pid"

	sleep 1

	if [ ! -e "/proc/$monitor_pid" ]; then
		RET=$ksft_skip
		log_test "mcast addr notification: iproute2 too old"
		return $RET
	fi

	ip link add name "$test_dev" type dummy
	check_err $? "failed to add dummy interface"
	ip link set "$test_dev" up
	check_err $? "failed to set dummy interface up"
	ip link del dev "$test_dev"
	check_err $? "Failed to delete dummy interface"
	sleep 1

	# There should be 4 line matches as follows.
	# 13: test-dummy1    inet6 mcast ff02::1 scope global 
	# 13: test-dummy1    inet mcast 224.0.0.1 scope global 
	# Deleted 13: test-dummy1    inet mcast 224.0.0.1 scope global 
	# Deleted 13: test-dummy1    inet6 mcast ff02::1 scope global 
	match_result=$(grep -cE "$test_dev.*(224.0.0.1|ff02::1)" "$tmpfile")
	if [ "$match_result" -ne 4 ]; then
		RET=$ksft_fail
	fi
	log_test "mcast addr notification: Expected 4 matches, got $match_result"
	return $RET
}

#check for needed privileges
if [ "$(id -u)" -ne 0 ];then
	RET=$ksft_skip
	log_test "need root privileges"
	exit $RET
fi

require_command ip

tests_run

exit $EXIT_STATUS

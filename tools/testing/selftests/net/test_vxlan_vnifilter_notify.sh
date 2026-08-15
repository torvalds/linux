#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2034,SC2154,SC2317,SC2329
#
# Test for VXLAN vnifilter netlink notifications (RTM_NEWTUNNEL /
# RTM_DELTUNNEL).
#
# Verifies that:
# - Adding a new VNI sends a notification
# - Adding a new VNI with a remote sends a notification
# - Deleting a VNI sends a notification
# - Re-adding an existing VNI with the same attributes does not send
#   a spurious notification
# - Updating an existing VNI's remote sends a notification
# - Deleting a non-existent VNI does not send a notification

source lib.sh

require_command bridge

VXLAN_DEV=vxlan100

ALL_TESTS="
	test_vni_add_notify
	test_vni_add_remote_notify
	test_vni_del_notify
	test_vni_readd_no_notify
	test_vni_update_remote_notify
	test_vni_del_nonexistent_no_notify
"

setup_prepare()
{
	setup_ns NS1
	defer cleanup_all_ns

	ip -n "$NS1" link add $VXLAN_DEV type vxlan dstport 4789 \
		local 10.0.0.1 nolearning external vnifilter
	ip -n "$NS1" link set $VXLAN_DEV up
}

# Run bridge monitor in the background, execute a command, then count
# the notification lines.
# Usage: vni_notify_check <command> [args...]
# Sets: NOTIFY_COUNT with the number of notifications observed.
vni_notify_check()
{
	local tmpf cmd_ret monitor_pid

	tmpf=$(mktemp)
	defer rm "$tmpf"

	defer_scope_push
		ip netns exec "$NS1" bridge monitor vni > "$tmpf" 2>/dev/null &
		monitor_pid=$!
		defer kill_process "$monitor_pid"

		sleep 0.5
		if [ ! -e "/proc/$monitor_pid" ]; then
			RET=$ksft_skip
			log_test "iproute2 'bridge monitor vni' not supported"
			return "$RET"
		fi

		"$@"
		cmd_ret=$?
		sleep 0.2
	defer_scope_pop

	NOTIFY_COUNT=$(grep -c "$VXLAN_DEV" "$tmpf")
	NOTIFY_COUNT=${NOTIFY_COUNT:-0}
	return "$cmd_ret"
}

# Adding a brand new VNI should produce a notification.
test_vni_add_notify()
{
	RET=0

	vni_notify_check \
		bridge -n "$NS1" vni add vni 1000 dev "$VXLAN_DEV"
	check_err $? "Failed to add VNI"

	[ "$NOTIFY_COUNT" -eq 1 ]
	check_err $? "Expected 1 notification for VNI add, got $NOTIFY_COUNT"

	bridge -n "$NS1" vni delete vni 1000 dev "$VXLAN_DEV" 2>/dev/null

	log_test "VNI add sends notification"
}

# Adding a VNI with a remote should produce a notification.
test_vni_add_remote_notify()
{
	RET=0

	vni_notify_check \
		bridge -n "$NS1" vni add vni 4000 remote 10.0.0.2 dev "$VXLAN_DEV"
	check_err $? "Failed to add VNI with remote"

	[ "$NOTIFY_COUNT" -eq 1 ]
	check_err $? "Expected 1 notification for VNI add with remote, got $NOTIFY_COUNT"

	bridge -n "$NS1" vni delete vni 4000 dev "$VXLAN_DEV"

	log_test "VNI add with remote sends notification"
}

# Deleting a VNI should produce a notification.
test_vni_del_notify()
{
	RET=0

	bridge -n "$NS1" vni add vni 2000 dev "$VXLAN_DEV"

	vni_notify_check \
		bridge -n "$NS1" vni delete vni 2000 dev "$VXLAN_DEV"
	check_err $? "Failed to delete VNI"

	[ "$NOTIFY_COUNT" -eq 1 ]
	check_err $? "Expected 1 notification for VNI del, got $NOTIFY_COUNT"

	log_test "VNI delete sends notification"
}

# Re-adding an existing VNI with the same attributes should not produce
# a notification.
test_vni_readd_no_notify()
{
	RET=0

	bridge -n "$NS1" vni add vni 3000 dev "$VXLAN_DEV"

	vni_notify_check \
		bridge -n "$NS1" vni add vni 3000 dev "$VXLAN_DEV"
	check_err $? "Failed to re-add VNI"

	[ "$NOTIFY_COUNT" -eq 0 ]
	check_err $? "Expected 0 notifications for VNI re-add, got $NOTIFY_COUNT"

	bridge -n "$NS1" vni delete vni 3000 dev "$VXLAN_DEV"

	log_test "VNI re-add does not send spurious notification"
}

# Updating an existing VNI's remote should produce a notification.
test_vni_update_remote_notify()
{
	RET=0

	bridge -n "$NS1" vni add vni 5000 remote 10.0.0.2 dev "$VXLAN_DEV"

	vni_notify_check \
		bridge -n "$NS1" vni add vni 5000 remote 10.0.0.3 dev "$VXLAN_DEV"
	check_err $? "Failed to update VNI remote"

	[ "$NOTIFY_COUNT" -eq 1 ]
	check_err $? "Expected 1 notification for VNI remote update, got $NOTIFY_COUNT"

	bridge -n "$NS1" vni delete vni 5000 dev "$VXLAN_DEV"

	log_test "VNI remote update sends notification"
}

# Deleting a non-existent VNI should not produce a notification.
test_vni_del_nonexistent_no_notify()
{
	RET=0

	vni_notify_check \
		bridge -n "$NS1" vni delete vni 9999 dev "$VXLAN_DEV" 2>/dev/null

	[ "$NOTIFY_COUNT" -eq 0 ]
	check_err $? "Expected 0 notifications for non-existent VNI del, got $NOTIFY_COUNT"

	log_test "Non-existent VNI delete does not send notification"
}

trap defer_scopes_cleanup EXIT

setup_prepare
tests_run

exit "$EXIT_STATUS"

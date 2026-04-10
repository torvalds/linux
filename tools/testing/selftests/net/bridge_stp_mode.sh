#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2034,SC2154,SC2317,SC2329
#
# Test for bridge STP mode selection (IFLA_BR_STP_MODE).
#
# Verifies that:
# - stp_mode defaults to auto on new bridges
# - stp_mode can be toggled between user, kernel, and auto
# - stp_mode change is rejected while STP is active (-EBUSY)
# - stp_mode user in a netns yields userspace STP (stp_state=2)
# - stp_mode kernel forces kernel STP (stp_state=1)
# - stp_mode auto preserves traditional fallback to kernel STP
# - stp_mode and stp_state can be set atomically in one message
# - stp_mode persists across STP disable/enable cycles

source lib.sh

require_command jq

ALL_TESTS="
	test_default_auto
	test_set_modes
	test_reject_change_while_stp_active
	test_idempotent_mode_while_stp_active
	test_user_mode_in_netns
	test_kernel_mode
	test_auto_mode
	test_atomic_mode_and_state
	test_mode_persistence
"

bridge_info_get()
{
	ip -n "$NS1" -d -j link show "$1" | \
		jq -r ".[0].linkinfo.info_data.$2"
}

check_stp_mode()
{
	local br=$1; shift
	local expected=$1; shift
	local msg=$1; shift
	local val

	val=$(bridge_info_get "$br" stp_mode)
	[ "$val" = "$expected" ]
	check_err $? "$msg: expected $expected, got $val"
}

check_stp_state()
{
	local br=$1; shift
	local expected=$1; shift
	local msg=$1; shift
	local val

	val=$(bridge_info_get "$br" stp_state)
	[ "$val" = "$expected" ]
	check_err $? "$msg: expected $expected, got $val"
}

# Create a bridge in NS1, bring it up, and defer its deletion.
bridge_create()
{
	ip -n "$NS1" link add "$1" type bridge
	ip -n "$NS1" link set "$1" up
	defer ip -n "$NS1" link del "$1"
}

setup_prepare()
{
	setup_ns NS1
}

cleanup()
{
	defer_scopes_cleanup
	cleanup_all_ns
}

# Check that stp_mode defaults to auto when creating a bridge.
test_default_auto()
{
	RET=0

	ip -n "$NS1" link add br-test type bridge
	defer ip -n "$NS1" link del br-test

	check_stp_mode br-test auto "stp_mode default"

	log_test "stp_mode defaults to auto"
}

# Test setting stp_mode to user, kernel, and back to auto.
test_set_modes()
{
	RET=0

	ip -n "$NS1" link add br-test type bridge
	defer ip -n "$NS1" link del br-test

	ip -n "$NS1" link set dev br-test type bridge stp_mode user
	check_err $? "Failed to set stp_mode to user"
	check_stp_mode br-test user "after set user"

	ip -n "$NS1" link set dev br-test type bridge stp_mode kernel
	check_err $? "Failed to set stp_mode to kernel"
	check_stp_mode br-test kernel "after set kernel"

	ip -n "$NS1" link set dev br-test type bridge stp_mode auto
	check_err $? "Failed to set stp_mode to auto"
	check_stp_mode br-test auto "after set auto"

	log_test "stp_mode set user/kernel/auto"
}

# Verify that stp_mode cannot be changed while STP is active.
test_reject_change_while_stp_active()
{
	RET=0

	bridge_create br-test

	ip -n "$NS1" link set dev br-test type bridge stp_mode kernel
	check_err $? "Failed to set stp_mode to kernel"

	ip -n "$NS1" link set dev br-test type bridge stp_state 1
	check_err $? "Failed to enable STP"

	# Changing stp_mode while STP is active should fail.
	ip -n "$NS1" link set dev br-test type bridge stp_mode auto 2>/dev/null
	check_fail $? "Changing stp_mode should fail while STP is active"

	check_stp_mode br-test kernel "mode unchanged after rejected change"

	# Disable STP, then change should succeed.
	ip -n "$NS1" link set dev br-test type bridge stp_state 0
	check_err $? "Failed to disable STP"

	ip -n "$NS1" link set dev br-test type bridge stp_mode auto
	check_err $? "Changing stp_mode should succeed after STP is disabled"

	log_test "reject stp_mode change while STP is active"
}

# Verify that re-setting the same stp_mode while STP is active succeeds.
test_idempotent_mode_while_stp_active()
{
	RET=0

	bridge_create br-test

	ip -n "$NS1" link set dev br-test type bridge stp_mode user stp_state 1
	check_err $? "Failed to enable STP with user mode"

	# Re-setting the same mode while STP is active should succeed.
	ip -n "$NS1" link set dev br-test type bridge stp_mode user
	check_err $? "Idempotent stp_mode set should succeed while STP is active"

	check_stp_state br-test 2 "stp_state after idempotent set"

	# Changing mode while disabling STP in the same message should succeed.
	ip -n "$NS1" link set dev br-test type bridge stp_mode auto stp_state 0
	check_err $? "Mode change with simultaneous STP disable should succeed"

	check_stp_mode br-test auto "mode changed after disable+change"
	check_stp_state br-test 0 "stp_state after disable+change"

	log_test "idempotent and simultaneous mode change while STP active"
}

# Test that stp_mode user in a non-init netns yields userspace STP
# (stp_state == 2). This is the key use case: userspace STP without
# needing /sbin/bridge-stp or being in init_net.
test_user_mode_in_netns()
{
	RET=0

	bridge_create br-test

	ip -n "$NS1" link set dev br-test type bridge stp_mode user
	check_err $? "Failed to set stp_mode to user"

	ip -n "$NS1" link set dev br-test type bridge stp_state 1
	check_err $? "Failed to enable STP"

	check_stp_state br-test 2 "stp_state with user mode"

	log_test "stp_mode user in netns yields userspace STP"
}

# Test that stp_mode kernel forces kernel STP (stp_state == 1)
# regardless of whether /sbin/bridge-stp exists.
test_kernel_mode()
{
	RET=0

	bridge_create br-test

	ip -n "$NS1" link set dev br-test type bridge stp_mode kernel
	check_err $? "Failed to set stp_mode to kernel"

	ip -n "$NS1" link set dev br-test type bridge stp_state 1
	check_err $? "Failed to enable STP"

	check_stp_state br-test 1 "stp_state with kernel mode"

	log_test "stp_mode kernel forces kernel STP"
}

# Test that stp_mode auto preserves traditional behavior: in a netns
# (non-init_net), bridge-stp is not called and STP falls back to
# kernel mode (stp_state == 1).
test_auto_mode()
{
	RET=0

	bridge_create br-test

	# Auto mode is the default; enable STP in a netns.
	ip -n "$NS1" link set dev br-test type bridge stp_state 1
	check_err $? "Failed to enable STP"

	# In a netns with auto mode, bridge-stp is skipped (init_net only),
	# so STP should fall back to kernel mode (stp_state == 1).
	check_stp_state br-test 1 "stp_state with auto mode in netns"

	log_test "stp_mode auto preserves traditional behavior"
}

# Test that stp_mode and stp_state can be set in a single netlink
# message. This is the intended atomic usage pattern.
test_atomic_mode_and_state()
{
	RET=0

	bridge_create br-test

	# Set both stp_mode and stp_state in one command.
	ip -n "$NS1" link set dev br-test type bridge stp_mode user stp_state 1
	check_err $? "Failed to set stp_mode user and stp_state 1 atomically"

	check_stp_state br-test 2 "stp_state after atomic set"

	log_test "atomic stp_mode user + stp_state 1 in single message"
}

# Test that stp_mode persists across STP disable/enable cycles.
test_mode_persistence()
{
	RET=0

	bridge_create br-test

	# Set user mode and enable STP.
	ip -n "$NS1" link set dev br-test type bridge stp_mode user
	ip -n "$NS1" link set dev br-test type bridge stp_state 1
	check_err $? "Failed to enable STP with user mode"

	# Disable STP.
	ip -n "$NS1" link set dev br-test type bridge stp_state 0
	check_err $? "Failed to disable STP"

	# Verify mode is still user.
	check_stp_mode br-test user "stp_mode after STP disable"

	# Re-enable STP -- should use user mode again.
	ip -n "$NS1" link set dev br-test type bridge stp_state 1
	check_err $? "Failed to re-enable STP"

	check_stp_state br-test 2 "stp_state after re-enable"

	log_test "stp_mode persists across STP disable/enable cycles"
}

# Check iproute2 support before setting up resources.
if ! ip link add type bridge help 2>&1 | grep -q "stp_mode"; then
	echo "SKIP: iproute2 too old, missing stp_mode support"
	exit "$ksft_skip"
fi

trap cleanup EXIT

setup_prepare
tests_run

exit "$EXIT_STATUS"

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking the FIB offload API on top of mlxsw.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	ipv4_identical_routes
	ipv4_tos
	ipv4_metric
	ipv4_replace
	ipv4_delete
	ipv4_plen
	ipv4_replay
	ipv4_flush
	ipv4_local_replace
	ipv6_add
	ipv6_metric
	ipv6_append_single
	ipv6_replace_single
	ipv6_metric_multipath
	ipv6_append_multipath
	ipv6_replace_multipath
	ipv6_append_multipath_to_single
	ipv6_delete_single
	ipv6_delete_multipath
	ipv6_replay_single
	ipv6_replay_multipath
	ipv6_local_replace
"
NUM_NETIFS=0
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh
source $lib_dir/fib_offload_lib.sh

ipv4_identical_routes()
{
	fib_ipv4_identical_routes_test "testns1"
}

ipv4_tos()
{
	fib_ipv4_tos_test "testns1"
}

ipv4_metric()
{
	fib_ipv4_metric_test "testns1"
}

ipv4_replace()
{
	fib_ipv4_replace_test "testns1"
}

ipv4_delete()
{
	fib_ipv4_delete_test "testns1"
}

ipv4_plen()
{
	fib_ipv4_plen_test "testns1"
}

ipv4_replay_metric()
{
	fib_ipv4_replay_metric_test "testns1" "$DEVLINK_DEV"
}

ipv4_replay_tos()
{
	fib_ipv4_replay_tos_test "testns1" "$DEVLINK_DEV"
}

ipv4_replay_plen()
{
	fib_ipv4_replay_plen_test "testns1" "$DEVLINK_DEV"
}

ipv4_replay()
{
	ipv4_replay_metric
	ipv4_replay_tos
	ipv4_replay_plen
}

ipv4_flush()
{
	fib_ipv4_flush_test "testns1"
}

ipv4_local_replace()
{
	local ns="testns1"

	RET=0

	ip -n $ns link add name dummy1 type dummy
	ip -n $ns link set dev dummy1 up

	ip -n $ns route add table local 192.0.2.1/32 dev dummy1
	fib4_trap_check $ns "table local 192.0.2.1/32 dev dummy1" false
	check_err $? "Local table route not in hardware when should"

	ip -n $ns route add table main 192.0.2.1/32 dev dummy1
	fib4_trap_check $ns "table main 192.0.2.1/32 dev dummy1" true
	check_err $? "Main table route in hardware when should not"

	fib4_trap_check $ns "table local 192.0.2.1/32 dev dummy1" false
	check_err $? "Local table route was replaced when should not"

	# Test that local routes can replace routes in main table.
	ip -n $ns route add table main 192.0.2.2/32 dev dummy1
	fib4_trap_check $ns "table main 192.0.2.2/32 dev dummy1" false
	check_err $? "Main table route not in hardware when should"

	ip -n $ns route add table local 192.0.2.2/32 dev dummy1
	fib4_trap_check $ns "table local 192.0.2.2/32 dev dummy1" false
	check_err $? "Local table route did not replace route in main table when should"

	fib4_trap_check $ns "table main 192.0.2.2/32 dev dummy1" true
	check_err $? "Main table route was not replaced when should"

	log_test "IPv4 local table route replacement"

	ip -n $ns link del dev dummy1
}

ipv6_add()
{
	fib_ipv6_add_test "testns1"
}

ipv6_metric()
{
	fib_ipv6_metric_test "testns1"
}

ipv6_append_single()
{
	fib_ipv6_append_single_test "testns1"
}

ipv6_replace_single()
{
	fib_ipv6_replace_single_test "testns1"
}

ipv6_metric_multipath()
{
	fib_ipv6_metric_multipath_test "testns1"
}

ipv6_append_multipath()
{
	fib_ipv6_append_multipath_test "testns1"
}

ipv6_replace_multipath()
{
	fib_ipv6_replace_multipath_test "testns1"
}

ipv6_append_multipath_to_single()
{
	fib_ipv6_append_multipath_to_single_test "testns1"
}

ipv6_delete_single()
{
	fib_ipv6_delete_single_test "testns1"
}

ipv6_delete_multipath()
{
	fib_ipv6_delete_multipath_test "testns1"
}

ipv6_replay_single()
{
	fib_ipv6_replay_single_test "testns1" "$DEVLINK_DEV"
}

ipv6_replay_multipath()
{
	fib_ipv6_replay_multipath_test "testns1" "$DEVLINK_DEV"
}

ipv6_local_replace()
{
	local ns="testns1"

	RET=0

	ip -n $ns link add name dummy1 type dummy
	ip -n $ns link set dev dummy1 up

	ip -n $ns route add table local 2001:db8:1::1/128 dev dummy1
	fib6_trap_check $ns "table local 2001:db8:1::1/128 dev dummy1" false
	check_err $? "Local table route not in hardware when should"

	ip -n $ns route add table main 2001:db8:1::1/128 dev dummy1
	fib6_trap_check $ns "table main 2001:db8:1::1/128 dev dummy1" true
	check_err $? "Main table route in hardware when should not"

	fib6_trap_check $ns "table local 2001:db8:1::1/128 dev dummy1" false
	check_err $? "Local table route was replaced when should not"

	# Test that local routes can replace routes in main table.
	ip -n $ns route add table main 2001:db8:1::2/128 dev dummy1
	fib6_trap_check $ns "table main 2001:db8:1::2/128 dev dummy1" false
	check_err $? "Main table route not in hardware when should"

	ip -n $ns route add table local 2001:db8:1::2/128 dev dummy1
	fib6_trap_check $ns "table local 2001:db8:1::2/128 dev dummy1" false
	check_err $? "Local route route did not replace route in main table when should"

	fib6_trap_check $ns "table main 2001:db8:1::2/128 dev dummy1" true
	check_err $? "Main table route was not replaced when should"

	log_test "IPv6 local table route replacement"

	ip -n $ns link del dev dummy1
}

setup_prepare()
{
	ip netns add testns1
	if [ $? -ne 0 ]; then
		echo "Failed to add netns \"testns1\""
		exit 1
	fi

	devlink dev reload $DEVLINK_DEV netns testns1
	if [ $? -ne 0 ]; then
		echo "Failed to reload into netns \"testns1\""
		exit 1
	fi
}

cleanup()
{
	pre_cleanup
	devlink -N testns1 dev reload $DEVLINK_DEV netns $$
	ip netns del testns1
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS

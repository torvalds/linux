#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking the FIB offload API. It makes use of netdevsim
# which registers a listener to the FIB notification chain.

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
	ipv4_error_path
	ipv4_delete_fail
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
	ipv6_error_path
	ipv6_delete_fail
"
NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV/net/
DEBUGFS_DIR=/sys/kernel/debug/netdevsim/$DEV/
NUM_NETIFS=0
source $lib_dir/lib.sh
source $lib_dir/fib_offload_lib.sh

DEVLINK_DEV=
source $lib_dir/devlink_lib.sh
DEVLINK_DEV=netdevsim/${DEV}

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

ipv4_error_path_add()
{
	local lsb

	RET=0

	ip -n testns1 link add name dummy1 type dummy
	ip -n testns1 link set dev dummy1 up

	devlink -N testns1 resource set $DEVLINK_DEV path IPv4/fib size 10
	devlink -N testns1 dev reload $DEVLINK_DEV

	for lsb in $(seq 1 20); do
		ip -n testns1 route add 192.0.2.${lsb}/32 dev dummy1 \
			&> /dev/null
	done

	log_test "IPv4 error path - add"

	ip -n testns1 link del dev dummy1
}

ipv4_error_path_replay()
{
	local lsb

	RET=0

	ip -n testns1 link add name dummy1 type dummy
	ip -n testns1 link set dev dummy1 up

	devlink -N testns1 resource set $DEVLINK_DEV path IPv4/fib size 100
	devlink -N testns1 dev reload $DEVLINK_DEV

	for lsb in $(seq 1 20); do
		ip -n testns1 route add 192.0.2.${lsb}/32 dev dummy1
	done

	devlink -N testns1 resource set $DEVLINK_DEV path IPv4/fib size 10
	devlink -N testns1 dev reload $DEVLINK_DEV &> /dev/null

	log_test "IPv4 error path - replay"

	ip -n testns1 link del dev dummy1

	# Successfully reload after deleting all the routes.
	devlink -N testns1 resource set $DEVLINK_DEV path IPv4/fib size 100
	devlink -N testns1 dev reload $DEVLINK_DEV
}

ipv4_error_path()
{
	# Test the different error paths of the notifiers by limiting the size
	# of the "IPv4/fib" resource.
	ipv4_error_path_add
	ipv4_error_path_replay
}

ipv4_delete_fail()
{
	RET=0

	echo "y" > $DEBUGFS_DIR/fib/fail_route_delete

	ip -n testns1 link add name dummy1 type dummy
	ip -n testns1 link set dev dummy1 up

	ip -n testns1 route add 192.0.2.0/24 dev dummy1
	ip -n testns1 route del 192.0.2.0/24 dev dummy1 &> /dev/null

	# We should not be able to delete the netdev if we are leaking a
	# reference.
	ip -n testns1 link del dev dummy1

	log_test "IPv4 route delete failure"

	echo "n" > $DEBUGFS_DIR/fib/fail_route_delete
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

ipv6_error_path_add_single()
{
	local lsb

	RET=0

	ip -n testns1 link add name dummy1 type dummy
	ip -n testns1 link set dev dummy1 up

	devlink -N testns1 resource set $DEVLINK_DEV path IPv6/fib size 10
	devlink -N testns1 dev reload $DEVLINK_DEV

	for lsb in $(seq 1 20); do
		ip -n testns1 route add 2001:db8:1::${lsb}/128 dev dummy1 \
			&> /dev/null
	done

	log_test "IPv6 error path - add single"

	ip -n testns1 link del dev dummy1
}

ipv6_error_path_add_multipath()
{
	local lsb

	RET=0

	for i in $(seq 1 2); do
		ip -n testns1 link add name dummy$i type dummy
		ip -n testns1 link set dev dummy$i up
		ip -n testns1 address add 2001:db8:$i::1/64 dev dummy$i
	done

	devlink -N testns1 resource set $DEVLINK_DEV path IPv6/fib size 10
	devlink -N testns1 dev reload $DEVLINK_DEV

	for lsb in $(seq 1 20); do
		ip -n testns1 route add 2001:db8:10::${lsb}/128 \
			nexthop via 2001:db8:1::2 dev dummy1 \
			nexthop via 2001:db8:2::2 dev dummy2 &> /dev/null
	done

	log_test "IPv6 error path - add multipath"

	for i in $(seq 1 2); do
		ip -n testns1 link del dev dummy$i
	done
}

ipv6_error_path_replay()
{
	local lsb

	RET=0

	ip -n testns1 link add name dummy1 type dummy
	ip -n testns1 link set dev dummy1 up

	devlink -N testns1 resource set $DEVLINK_DEV path IPv6/fib size 100
	devlink -N testns1 dev reload $DEVLINK_DEV

	for lsb in $(seq 1 20); do
		ip -n testns1 route add 2001:db8:1::${lsb}/128 dev dummy1
	done

	devlink -N testns1 resource set $DEVLINK_DEV path IPv6/fib size 10
	devlink -N testns1 dev reload $DEVLINK_DEV &> /dev/null

	log_test "IPv6 error path - replay"

	ip -n testns1 link del dev dummy1

	# Successfully reload after deleting all the routes.
	devlink -N testns1 resource set $DEVLINK_DEV path IPv6/fib size 100
	devlink -N testns1 dev reload $DEVLINK_DEV
}

ipv6_error_path()
{
	# Test the different error paths of the notifiers by limiting the size
	# of the "IPv6/fib" resource.
	ipv6_error_path_add_single
	ipv6_error_path_add_multipath
	ipv6_error_path_replay
}

ipv6_delete_fail()
{
	RET=0

	echo "y" > $DEBUGFS_DIR/fib/fail_route_delete

	ip -n testns1 link add name dummy1 type dummy
	ip -n testns1 link set dev dummy1 up

	ip -n testns1 route add 2001:db8:1::/64 dev dummy1
	ip -n testns1 route del 2001:db8:1::/64 dev dummy1 &> /dev/null

	# We should not be able to delete the netdev if we are leaking a
	# reference.
	ip -n testns1 link del dev dummy1

	log_test "IPv6 route delete failure"

	echo "n" > $DEBUGFS_DIR/fib/fail_route_delete
}

fib_notify_on_flag_change_set()
{
	local notify=$1; shift

	ip netns exec testns1 sysctl -qw net.ipv4.fib_notify_on_flag_change=$notify
	ip netns exec testns1 sysctl -qw net.ipv6.fib_notify_on_flag_change=$notify

	log_info "Set fib_notify_on_flag_change to $notify"
}

setup_prepare()
{
	local netdev

	modprobe netdevsim &> /dev/null

	echo "$DEV_ADDR 1" > ${NETDEVSIM_PATH}/new_device
	while [ ! -d $SYSFS_NET_DIR ] ; do :; done

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
	ip netns del testns1
	echo "$DEV_ADDR" > ${NETDEVSIM_PATH}/del_device
	modprobe -r netdevsim &> /dev/null
}

trap cleanup EXIT

setup_prepare

fib_notify_on_flag_change_set 1
tests_run

fib_notify_on_flag_change_set 0
tests_run

exit $EXIT_STATUS

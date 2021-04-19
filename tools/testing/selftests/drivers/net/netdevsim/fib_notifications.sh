#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	ipv4_route_addition_test
	ipv4_route_deletion_test
	ipv4_route_replacement_test
	ipv4_route_offload_failed_test
	ipv6_route_addition_test
	ipv6_route_deletion_test
	ipv6_route_replacement_test
	ipv6_route_offload_failed_test
"

NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
DEVLINK_DEV=netdevsim/${DEV}
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV/net/
DEBUGFS_DIR=/sys/kernel/debug/netdevsim/$DEV/
NUM_NETIFS=0
source $lib_dir/lib.sh

check_rt_offload_failed()
{
	local outfile=$1; shift
	local line

	# Make sure that the first notification was emitted without
	# RTM_F_OFFLOAD_FAILED flag and the second with RTM_F_OFFLOAD_FAILED
	# flag
	head -n 1 $outfile | grep -q "rt_offload_failed"
	if [[ $? -eq 0 ]]; then
		return 1
	fi

	head -n 2 $outfile | tail -n 1 | grep -q "rt_offload_failed"
}

check_rt_trap()
{
	local outfile=$1; shift
	local line

	# Make sure that the first notification was emitted without RTM_F_TRAP
	# flag and the second with RTM_F_TRAP flag
	head -n 1 $outfile | grep -q "rt_trap"
	if [[ $? -eq 0 ]]; then
		return 1
	fi

	head -n 2 $outfile | tail -n 1 | grep -q "rt_trap"
}

route_notify_check()
{
	local outfile=$1; shift
	local expected_num_lines=$1; shift
	local offload_failed=${1:-0}; shift

	# check the monitor results
	lines=`wc -l $outfile | cut "-d " -f1`
	test $lines -eq $expected_num_lines
	check_err $? "$expected_num_lines notifications were expected but $lines were received"

	if [[ $expected_num_lines -eq 1 ]]; then
		return
	fi

	if [[ $offload_failed -eq 0 ]]; then
		check_rt_trap $outfile
		check_err $? "Wrong RTM_F_TRAP flags in notifications"
	else
		check_rt_offload_failed $outfile
		check_err $? "Wrong RTM_F_OFFLOAD_FAILED flags in notifications"
	fi
}

route_addition_check()
{
	local ip=$1; shift
	local notify=$1; shift
	local route=$1; shift
	local expected_num_notifications=$1; shift
	local offload_failed=${1:-0}; shift

	ip netns exec testns1 sysctl -qw net.$ip.fib_notify_on_flag_change=$notify

	local outfile=$(mktemp)

	$IP monitor route &> $outfile &
	sleep 1
	$IP route add $route dev dummy1
	sleep 1
	kill %% && wait %% &> /dev/null

	route_notify_check $outfile $expected_num_notifications $offload_failed
	rm -f $outfile

	$IP route del $route dev dummy1
}

ipv4_route_addition_test()
{
	RET=0

	local ip="ipv4"
	local route=192.0.2.0/24

	# Make sure a single notification will be emitted for the programmed
	# route.
	local notify=0
	local expected_num_notifications=1
	# route_addition_check will assign value to RET.
	route_addition_check $ip $notify $route $expected_num_notifications

	# Make sure two notifications will be emitted for the programmed route.
	notify=1
	expected_num_notifications=2
	route_addition_check $ip $notify $route $expected_num_notifications

	# notify=2 means emit notifications only for failed route installation,
	# make sure a single notification will be emitted for the programmed
	# route.
	notify=2
	expected_num_notifications=1
	route_addition_check $ip $notify $route $expected_num_notifications

	log_test "IPv4 route addition"
}

route_deletion_check()
{
	local ip=$1; shift
	local notify=$1; shift
	local route=$1; shift
	local expected_num_notifications=$1; shift

	ip netns exec testns1 sysctl -qw net.$ip.fib_notify_on_flag_change=$notify
	$IP route add $route dev dummy1
	sleep 1

	local outfile=$(mktemp)

	$IP monitor route &> $outfile &
	sleep 1
	$IP route del $route dev dummy1
	sleep 1
	kill %% && wait %% &> /dev/null

	route_notify_check $outfile $expected_num_notifications
	rm -f $outfile
}

ipv4_route_deletion_test()
{
	RET=0

	local ip="ipv4"
	local route=192.0.2.0/24
	local expected_num_notifications=1

	# Make sure a single notification will be emitted for the deleted route,
	# regardless of fib_notify_on_flag_change value.
	local notify=0
	# route_deletion_check will assign value to RET.
	route_deletion_check $ip $notify $route $expected_num_notifications

	notify=1
	route_deletion_check $ip $notify $route $expected_num_notifications

	log_test "IPv4 route deletion"
}

route_replacement_check()
{
	local ip=$1; shift
	local notify=$1; shift
	local route=$1; shift
	local expected_num_notifications=$1; shift

	ip netns exec testns1 sysctl -qw net.$ip.fib_notify_on_flag_change=$notify
	$IP route add $route dev dummy1
	sleep 1

	local outfile=$(mktemp)

	$IP monitor route &> $outfile &
	sleep 1
	$IP route replace $route dev dummy2
	sleep 1
	kill %% && wait %% &> /dev/null

	route_notify_check $outfile $expected_num_notifications
	rm -f $outfile

	$IP route del $route dev dummy2
}

ipv4_route_replacement_test()
{
	RET=0

	local ip="ipv4"
	local route=192.0.2.0/24

	$IP link add name dummy2 type dummy
	$IP link set dev dummy2 up

	# Make sure a single notification will be emitted for the new route.
	local notify=0
	local expected_num_notifications=1
	# route_replacement_check will assign value to RET.
	route_replacement_check $ip $notify $route $expected_num_notifications

	# Make sure two notifications will be emitted for the new route.
	notify=1
	expected_num_notifications=2
	route_replacement_check $ip $notify $route $expected_num_notifications

	# notify=2 means emit notifications only for failed route installation,
	# make sure a single notification will be emitted for the new route.
	notify=2
	expected_num_notifications=1
	route_replacement_check $ip $notify $route $expected_num_notifications

	$IP link del name dummy2

	log_test "IPv4 route replacement"
}

ipv4_route_offload_failed_test()
{

	RET=0

	local ip="ipv4"
	local route=192.0.2.0/24
	local offload_failed=1

	echo "y"> $DEBUGFS_DIR/fib/fail_route_offload
	check_err $? "Failed to setup route offload to fail"

	# Make sure a single notification will be emitted for the programmed
	# route.
	local notify=0
	local expected_num_notifications=1
	route_addition_check $ip $notify $route $expected_num_notifications \
		$offload_failed

	# Make sure two notifications will be emitted for the new route.
	notify=1
	expected_num_notifications=2
	route_addition_check $ip $notify $route $expected_num_notifications \
		$offload_failed

	# notify=2 means emit notifications only for failed route installation,
	# make sure two notifications will be emitted for the new route.
	notify=2
	expected_num_notifications=2
	route_addition_check $ip $notify $route $expected_num_notifications \
		$offload_failed

	echo "n"> $DEBUGFS_DIR/fib/fail_route_offload
	check_err $? "Failed to setup route offload not to fail"

	log_test "IPv4 route offload failed"
}

ipv6_route_addition_test()
{
	RET=0

	local ip="ipv6"
	local route=2001:db8:1::/64

	# Make sure a single notification will be emitted for the programmed
	# route.
	local notify=0
	local expected_num_notifications=1
	route_addition_check $ip $notify $route $expected_num_notifications

	# Make sure two notifications will be emitted for the programmed route.
	notify=1
	expected_num_notifications=2
	route_addition_check $ip $notify $route $expected_num_notifications

	# notify=2 means emit notifications only for failed route installation,
	# make sure a single notification will be emitted for the programmed
	# route.
	notify=2
	expected_num_notifications=1
	route_addition_check $ip $notify $route $expected_num_notifications

	log_test "IPv6 route addition"
}

ipv6_route_deletion_test()
{
	RET=0

	local ip="ipv6"
	local route=2001:db8:1::/64
	local expected_num_notifications=1

	# Make sure a single notification will be emitted for the deleted route,
	# regardless of fib_notify_on_flag_change value.
	local notify=0
	route_deletion_check $ip $notify $route $expected_num_notifications

	notify=1
	route_deletion_check $ip $notify $route $expected_num_notifications

	log_test "IPv6 route deletion"
}

ipv6_route_replacement_test()
{
	RET=0

	local ip="ipv6"
	local route=2001:db8:1::/64

	$IP link add name dummy2 type dummy
	$IP link set dev dummy2 up

	# Make sure a single notification will be emitted for the new route.
	local notify=0
	local expected_num_notifications=1
	route_replacement_check $ip $notify $route $expected_num_notifications

	# Make sure two notifications will be emitted for the new route.
	notify=1
	expected_num_notifications=2
	route_replacement_check $ip $notify $route $expected_num_notifications

	# notify=2 means emit notifications only for failed route installation,
	# make sure a single notification will be emitted for the new route.
	notify=2
	expected_num_notifications=1
	route_replacement_check $ip $notify $route $expected_num_notifications

	$IP link del name dummy2

	log_test "IPv6 route replacement"
}

ipv6_route_offload_failed_test()
{

	RET=0

	local ip="ipv6"
	local route=2001:db8:1::/64
	local offload_failed=1

	echo "y"> $DEBUGFS_DIR/fib/fail_route_offload
	check_err $? "Failed to setup route offload to fail"

	# Make sure a single notification will be emitted for the programmed
	# route.
	local notify=0
	local expected_num_notifications=1
	route_addition_check $ip $notify $route $expected_num_notifications \
		$offload_failed

	# Make sure two notifications will be emitted for the new route.
	notify=1
	expected_num_notifications=2
	route_addition_check $ip $notify $route $expected_num_notifications \
		$offload_failed

	# notify=2 means emit notifications only for failed route installation,
	# make sure two notifications will be emitted for the new route.
	notify=2
	expected_num_notifications=2
	route_addition_check $ip $notify $route $expected_num_notifications \
		$offload_failed

	echo "n"> $DEBUGFS_DIR/fib/fail_route_offload
	check_err $? "Failed to setup route offload not to fail"

	log_test "IPv6 route offload failed"
}

setup_prepare()
{
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

	IP="ip -n testns1"

	$IP link add name dummy1 type dummy
	$IP link set dev dummy1 up
}

cleanup()
{
	pre_cleanup

	$IP link del name dummy1
	ip netns del testns1
	echo "$DEV_ADDR" > ${NETDEVSIM_PATH}/del_device
	modprobe -r netdevsim &> /dev/null
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS

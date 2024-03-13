#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	l3_reporting_test
	l3_fail_next_test
	l3_counter_test
	l3_rollback_test
	l3_monitor_test
"

NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR_1=1337
DEV_ADDR_2=1057
DEV_ADDR_3=5417
NUM_NETIFS=0
source $lib_dir/lib.sh

DUMMY_IFINDEX=

DEV_ADDR()
{
	local n=$1; shift
	local var=DEV_ADDR_$n

	echo ${!var}
}

DEV()
{
	echo netdevsim$(DEV_ADDR $1)
}

DEVLINK_DEV()
{
	echo netdevsim/$(DEV $1)
}

SYSFS_NET_DIR()
{
	echo /sys/bus/netdevsim/devices/$(DEV $1)/net/
}

DEBUGFS_DIR()
{
	echo /sys/kernel/debug/netdevsim/$(DEV $1)/
}

nsim_add()
{
	local n=$1; shift

	echo "$(DEV_ADDR $n) 1" > ${NETDEVSIM_PATH}/new_device
	while [ ! -d $(SYSFS_NET_DIR $n) ] ; do :; done
}

nsim_reload()
{
	local n=$1; shift
	local ns=$1; shift

	devlink dev reload $(DEVLINK_DEV $n) netns $ns

	if [ $? -ne 0 ]; then
		echo "Failed to reload $(DEV $n) into netns \"testns1\""
		exit 1
	fi

}

nsim_del()
{
	local n=$1; shift

	echo "$(DEV_ADDR $n)" > ${NETDEVSIM_PATH}/del_device
}

nsim_hwstats_toggle()
{
	local action=$1; shift
	local instance=$1; shift
	local netdev=$1; shift
	local type=$1; shift

	local ifindex=$($IP -j link show dev $netdev | jq '.[].ifindex')

	echo $ifindex > $(DEBUGFS_DIR $instance)/hwstats/$type/$action
}

nsim_hwstats_enable()
{
	nsim_hwstats_toggle enable_ifindex "$@"
}

nsim_hwstats_disable()
{
	nsim_hwstats_toggle disable_ifindex "$@"
}

nsim_hwstats_fail_next_enable()
{
	nsim_hwstats_toggle fail_next_enable "$@"
}

setup_prepare()
{
	modprobe netdevsim &> /dev/null
	nsim_add 1
	nsim_add 2
	nsim_add 3

	ip netns add testns1

	if [ $? -ne 0 ]; then
		echo "Failed to add netns \"testns1\""
		exit 1
	fi

	nsim_reload 1 testns1
	nsim_reload 2 testns1
	nsim_reload 3 testns1

	IP="ip -n testns1"

	$IP link add name dummy1 type dummy
	$IP link set dev dummy1 up
	DUMMY_IFINDEX=$($IP -j link show dev dummy1 | jq '.[].ifindex')
}

cleanup()
{
	pre_cleanup

	$IP link del name dummy1
	ip netns del testns1
	nsim_del 3
	nsim_del 2
	nsim_del 1
	modprobe -r netdevsim &> /dev/null
}

netdev_hwstats_used()
{
	local netdev=$1; shift
	local type=$1; shift

	$IP -j stats show dev "$netdev" group offload subgroup hw_stats_info |
	    jq '.[].info.l3_stats.used'
}

netdev_check_used()
{
	local netdev=$1; shift
	local type=$1; shift

	[[ $(netdev_hwstats_used $netdev $type) == "true" ]]
}

netdev_check_unused()
{
	local netdev=$1; shift
	local type=$1; shift

	[[ $(netdev_hwstats_used $netdev $type) == "false" ]]
}

netdev_hwstats_request()
{
	local netdev=$1; shift
	local type=$1; shift

	$IP -j stats show dev "$netdev" group offload subgroup hw_stats_info |
	    jq ".[].info.${type}_stats.request"
}

netdev_check_requested()
{
	local netdev=$1; shift
	local type=$1; shift

	[[ $(netdev_hwstats_request $netdev $type) == "true" ]]
}

netdev_check_unrequested()
{
	local netdev=$1; shift
	local type=$1; shift

	[[ $(netdev_hwstats_request $netdev $type) == "false" ]]
}

reporting_test()
{
	local type=$1; shift
	local instance=1

	RET=0

	[[ -n $(netdev_hwstats_used dummy1 $type) ]]
	check_err $? "$type stats not reported"

	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used before either device or netdevsim request"

	nsim_hwstats_enable $instance dummy1 $type
	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used before device request"
	netdev_check_unrequested dummy1 $type
	check_err $? "$type stats reported as requested before device request"

	$IP stats set dev dummy1 ${type}_stats on
	netdev_check_used dummy1 $type
	check_err $? "$type stats reported as not used after both device and netdevsim request"
	netdev_check_requested dummy1 $type
	check_err $? "$type stats reported as not requested after device request"

	nsim_hwstats_disable $instance dummy1 $type
	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used after netdevsim request withdrawn"

	nsim_hwstats_enable $instance dummy1 $type
	netdev_check_used dummy1 $type
	check_err $? "$type stats reported as not used after netdevsim request reenabled"

	$IP stats set dev dummy1 ${type}_stats off
	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used after device request withdrawn"
	netdev_check_unrequested dummy1 $type
	check_err $? "$type stats reported as requested after device request withdrawn"

	nsim_hwstats_disable $instance dummy1 $type
	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used after both requests withdrawn"

	log_test "Reporting of $type stats usage"
}

l3_reporting_test()
{
	reporting_test l3
}

__fail_next_test()
{
	local instance=$1; shift
	local type=$1; shift

	RET=0

	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used before either device or netdevsim request"

	nsim_hwstats_enable $instance dummy1 $type
	nsim_hwstats_fail_next_enable $instance dummy1 $type
	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used before device request"
	netdev_check_unrequested dummy1 $type
	check_err $? "$type stats reported as requested before device request"

	$IP stats set dev dummy1 ${type}_stats on 2>/dev/null
	check_fail $? "$type stats request not bounced as it should have been"
	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used after bounce"
	netdev_check_unrequested dummy1 $type
	check_err $? "$type stats reported as requested after bounce"

	$IP stats set dev dummy1 ${type}_stats on
	check_err $? "$type stats request failed when it shouldn't have"
	netdev_check_used dummy1 $type
	check_err $? "$type stats reported as not used after both device and netdevsim request"
	netdev_check_requested dummy1 $type
	check_err $? "$type stats reported as not requested after device request"

	$IP stats set dev dummy1 ${type}_stats off
	nsim_hwstats_disable $instance dummy1 $type

	log_test "Injected failure of $type stats enablement (netdevsim #$instance)"
}

fail_next_test()
{
	__fail_next_test 1 "$@"
	__fail_next_test 2 "$@"
	__fail_next_test 3 "$@"
}

l3_fail_next_test()
{
	fail_next_test l3
}

get_hwstat()
{
	local netdev=$1; shift
	local type=$1; shift
	local selector=$1; shift

	$IP -j stats show dev $netdev group offload subgroup ${type}_stats |
		  jq ".[0].stats64.${selector}"
}

counter_test()
{
	local type=$1; shift
	local instance=1

	RET=0

	nsim_hwstats_enable $instance dummy1 $type
	$IP stats set dev dummy1 ${type}_stats on
	netdev_check_used dummy1 $type
	check_err $? "$type stats reported as not used after both device and netdevsim request"

	# Netdevsim counts 10pps on ingress. We should see maybe a couple
	# packets, unless things take a reeealy long time.
	local pkts=$(get_hwstat dummy1 l3 rx.packets)
	((pkts < 10))
	check_err $? "$type stats show >= 10 packets after first enablement"

	sleep 2.5

	local pkts=$(get_hwstat dummy1 l3 rx.packets)
	((pkts >= 20))
	check_err $? "$type stats show < 20 packets after 2.5s passed"

	$IP stats set dev dummy1 ${type}_stats off

	sleep 2

	$IP stats set dev dummy1 ${type}_stats on
	local pkts=$(get_hwstat dummy1 l3 rx.packets)
	((pkts < 10))
	check_err $? "$type stats show >= 10 packets after second enablement"

	$IP stats set dev dummy1 ${type}_stats off
	nsim_hwstats_fail_next_enable $instance dummy1 $type
	$IP stats set dev dummy1 ${type}_stats on 2>/dev/null
	check_fail $? "$type stats request not bounced as it should have been"

	sleep 2

	$IP stats set dev dummy1 ${type}_stats on
	local pkts=$(get_hwstat dummy1 l3 rx.packets)
	((pkts < 10))
	check_err $? "$type stats show >= 10 packets after post-fail enablement"

	$IP stats set dev dummy1 ${type}_stats off

	log_test "Counter values in $type stats"
}

l3_counter_test()
{
	counter_test l3
}

rollback_test()
{
	local type=$1; shift

	RET=0

	nsim_hwstats_enable 1 dummy1 l3
	nsim_hwstats_enable 2 dummy1 l3
	nsim_hwstats_enable 3 dummy1 l3

	# The three netdevsim instances are registered in order of their number
	# one after another. It is reasonable to expect that whatever
	# notifications take place hit no. 2 in between hitting nos. 1 and 3,
	# whatever the actual order. This allows us to test that a fail caused
	# by no. 2 does not leave the system in a partial state, and rolls
	# everything back.

	nsim_hwstats_fail_next_enable 2 dummy1 l3
	$IP stats set dev dummy1 ${type}_stats on 2>/dev/null
	check_fail $? "$type stats request not bounced as it should have been"

	netdev_check_unused dummy1 $type
	check_err $? "$type stats reported as used after bounce"
	netdev_check_unrequested dummy1 $type
	check_err $? "$type stats reported as requested after bounce"

	sleep 2

	$IP stats set dev dummy1 ${type}_stats on
	check_err $? "$type stats request not upheld as it should have been"

	local pkts=$(get_hwstat dummy1 l3 rx.packets)
	((pkts < 10))
	check_err $? "$type stats show $pkts packets after post-fail enablement"

	$IP stats set dev dummy1 ${type}_stats off

	nsim_hwstats_disable 3 dummy1 l3
	nsim_hwstats_disable 2 dummy1 l3
	nsim_hwstats_disable 1 dummy1 l3

	log_test "Failure in $type stats enablement rolled back"
}

l3_rollback_test()
{
	rollback_test l3
}

l3_monitor_test()
{
	hw_stats_monitor_test dummy1 l3		   \
		"nsim_hwstats_enable 1 dummy1 l3"  \
		"nsim_hwstats_disable 1 dummy1 l3" \
		"$IP"
}

trap cleanup EXIT

setup_prepare
tests_run

exit $EXIT_STATUS

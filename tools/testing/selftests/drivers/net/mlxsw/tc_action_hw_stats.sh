#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	default_hw_stats_test
	immediate_hw_stats_test
	delayed_hw_stats_test
	disabled_hw_stats_test
"
NUM_NETIFS=2

source $lib_dir/tc_common.sh
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24
}

switch_create()
{
	simple_if_init $swp1 192.0.2.2/24
	tc qdisc add dev $swp1 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp1 clsact
	simple_if_fini $swp1 192.0.2.2/24
}

hw_stats_test()
{
	RET=0

	local name=$1
	local action_hw_stats=$2
	local occ_delta=$3
	local expected_packet_count=$4

	local orig_occ=$(devlink_resource_get "counters" "flow" | jq '.["occ"]')

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop $action_hw_stats
	check_err $? "Failed to add rule with $name hw_stats"

	local new_occ=$(devlink_resource_get "counters" "flow" | jq '.["occ"]')
	local expected_occ=$((orig_occ + occ_delta))
	[ "$new_occ" == "$expected_occ" ]
	check_err $? "Expected occupancy of $expected_occ, got $new_occ"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $swp1mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $swp1 ingress" 101 $expected_packet_count
	check_err $? "Did not match incoming packet"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	log_test "$name hw_stats"
}

default_hw_stats_test()
{
	hw_stats_test "default" "" 2 1
}

immediate_hw_stats_test()
{
	hw_stats_test "immediate" "hw_stats immediate" 2 1
}

delayed_hw_stats_test()
{
	RET=0

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop hw_stats delayed
	check_fail $? "Unexpected success in adding rule with delayed hw_stats"

	log_test "delayed hw_stats"
}

disabled_hw_stats_test()
{
	hw_stats_test "disabled" "hw_stats disabled" 0 0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	h1mac=$(mac_get $h1)
	swp1mac=$(mac_get $swp1)

	vrf_prepare

	h1_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h1_destroy

	vrf_cleanup
}

check_tc_action_hw_stats_support

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

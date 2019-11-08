#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap L2 drops functionality over mlxsw. Each registered L2 drop
# packet trap is tested to make sure it is triggered under the right
# conditions.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	source_mac_is_multicast_test
	vlan_tag_mismatch_test
	ingress_vlan_filter_test
	ingress_stp_filter_test
	port_list_is_empty_test
	port_loopback_filter_test
"
NUM_NETIFS=4
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1
}

h1_destroy()
{
	simple_if_fini $h1
}

h2_create()
{
	simple_if_init $h2
}

h2_destroy()
{
	simple_if_fini $h2
}

switch_create()
{
	ip link add dev br0 type bridge vlan_filtering 1 mcast_snooping 0

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact

	ip link set dev $swp2 down
	ip link set dev $swp1 down

	ip link del dev br0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare

	h1_create
	h2_create

	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy

	h2_destroy
	h1_destroy

	vrf_cleanup
}

source_mac_is_multicast_test()
{
	local trap_name="source_mac_is_multicast"
	local smac=01:02:03:04:05:06
	local group_name="l2_drops"
	local mz_pid

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower src_mac $smac action drop

	$MZ $h1 -c 0 -p 100 -a $smac -b bcast -t ip -d 1msec -q &
	mz_pid=$!

	RET=0

	devlink_trap_drop_test $trap_name $group_name $swp2

	log_test "Source MAC is multicast"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip
}

__vlan_tag_mismatch_test()
{
	local trap_name="vlan_tag_mismatch"
	local dmac=de:ad:be:ef:13:37
	local group_name="l2_drops"
	local opt=$1; shift
	local mz_pid

	# Remove PVID flag. This should prevent untagged and prio-tagged
	# packets from entering the bridge.
	bridge vlan add vid 1 dev $swp1 untagged master

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_mac $dmac action drop

	$MZ $h1 "$opt" -c 0 -p 100 -a own -b $dmac -t ip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $swp2

	# Add PVID and make sure packets are no longer dropped.
	bridge vlan add vid 1 dev $swp1 pvid untagged master
	devlink_trap_action_set $trap_name "trap"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle when packets should not be dropped"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with when packets should not be dropped"

	tc_check_packets "dev $swp2 egress" 101 0
	check_fail $? "Packets not forwarded when should"

	devlink_trap_action_set $trap_name "drop"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip
}

vlan_tag_mismatch_untagged_test()
{
	RET=0

	__vlan_tag_mismatch_test

	log_test "VLAN tag mismatch - untagged packets"
}

vlan_tag_mismatch_vid_0_test()
{
	RET=0

	__vlan_tag_mismatch_test "-Q 0"

	log_test "VLAN tag mismatch - prio-tagged packets"
}

vlan_tag_mismatch_test()
{
	vlan_tag_mismatch_untagged_test
	vlan_tag_mismatch_vid_0_test
}

ingress_vlan_filter_test()
{
	local trap_name="ingress_vlan_filter"
	local dmac=de:ad:be:ef:13:37
	local group_name="l2_drops"
	local mz_pid
	local vid=10

	bridge vlan add vid $vid dev $swp2 master

	RET=0

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_mac $dmac action drop

	$MZ $h1 -Q $vid -c 0 -p 100 -a own -b $dmac -t ip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $swp2

	# Add the VLAN on the bridge port and make sure packets are no longer
	# dropped.
	bridge vlan add vid $vid dev $swp1 master
	devlink_trap_action_set $trap_name "trap"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle when packets should not be dropped"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with when packets should not be dropped"

	tc_check_packets "dev $swp2 egress" 101 0
	check_fail $? "Packets not forwarded when should"

	devlink_trap_action_set $trap_name "drop"

	log_test "Ingress VLAN filter"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip

	bridge vlan del vid $vid dev $swp1 master
	bridge vlan del vid $vid dev $swp2 master
}

__ingress_stp_filter_test()
{
	local trap_name="ingress_spanning_tree_filter"
	local dmac=de:ad:be:ef:13:37
	local group_name="l2_drops"
	local state=$1; shift
	local mz_pid
	local vid=20

	bridge vlan add vid $vid dev $swp2 master
	bridge vlan add vid $vid dev $swp1 master
	ip link set dev $swp1 type bridge_slave state $state

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_mac $dmac action drop

	$MZ $h1 -Q $vid -c 0 -p 100 -a own -b $dmac -t ip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $swp2

	# Change STP state to forwarding and make sure packets are no longer
	# dropped.
	ip link set dev $swp1 type bridge_slave state 3
	devlink_trap_action_set $trap_name "trap"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle when packets should not be dropped"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with when packets should not be dropped"

	tc_check_packets "dev $swp2 egress" 101 0
	check_fail $? "Packets not forwarded when should"

	devlink_trap_action_set $trap_name "drop"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip

	bridge vlan del vid $vid dev $swp1 master
	bridge vlan del vid $vid dev $swp2 master
}

ingress_stp_filter_listening_test()
{
	local state=$1; shift

	RET=0

	__ingress_stp_filter_test $state

	log_test "Ingress STP filter - listening state"
}

ingress_stp_filter_learning_test()
{
	local state=$1; shift

	RET=0

	__ingress_stp_filter_test $state

	log_test "Ingress STP filter - learning state"
}

ingress_stp_filter_test()
{
	ingress_stp_filter_listening_test 1
	ingress_stp_filter_learning_test 2
}

port_list_is_empty_uc_test()
{
	local trap_name="port_list_is_empty"
	local dmac=de:ad:be:ef:13:37
	local group_name="l2_drops"
	local mz_pid

	# Disable unicast flooding on both ports, so that packets cannot egress
	# any port.
	ip link set dev $swp1 type bridge_slave flood off
	ip link set dev $swp2 type bridge_slave flood off

	RET=0

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_mac $dmac action drop

	$MZ $h1 -c 0 -p 100 -a own -b $dmac -t ip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $swp2

	# Allow packets to be flooded to one port.
	ip link set dev $swp2 type bridge_slave flood on
	devlink_trap_action_set $trap_name "trap"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle when packets should not be dropped"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with when packets should not be dropped"

	tc_check_packets "dev $swp2 egress" 101 0
	check_fail $? "Packets not forwarded when should"

	devlink_trap_action_set $trap_name "drop"

	log_test "Port list is empty - unicast"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip

	ip link set dev $swp1 type bridge_slave flood on
}

port_list_is_empty_mc_test()
{
	local trap_name="port_list_is_empty"
	local dmac=01:00:5e:00:00:01
	local group_name="l2_drops"
	local dip=239.0.0.1
	local mz_pid

	# Disable multicast flooding on both ports, so that packets cannot
	# egress any port. We also need to flush IP addresses from the bridge
	# in order to prevent packets from being flooded to the router port.
	ip link set dev $swp1 type bridge_slave mcast_flood off
	ip link set dev $swp2 type bridge_slave mcast_flood off
	ip address flush dev br0

	RET=0

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_mac $dmac action drop

	$MZ $h1 -c 0 -p 100 -a own -b $dmac -t ip -B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $swp2

	# Allow packets to be flooded to one port.
	ip link set dev $swp2 type bridge_slave mcast_flood on
	devlink_trap_action_set $trap_name "trap"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle when packets should not be dropped"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with when packets should not be dropped"

	tc_check_packets "dev $swp2 egress" 101 0
	check_fail $? "Packets not forwarded when should"

	devlink_trap_action_set $trap_name "drop"

	log_test "Port list is empty - multicast"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip

	ip link set dev $swp1 type bridge_slave mcast_flood on
}

port_list_is_empty_test()
{
	port_list_is_empty_uc_test
	port_list_is_empty_mc_test
}

port_loopback_filter_uc_test()
{
	local trap_name="port_loopback_filter"
	local dmac=de:ad:be:ef:13:37
	local group_name="l2_drops"
	local mz_pid

	# Make sure packets can only egress the input port.
	ip link set dev $swp2 type bridge_slave flood off

	RET=0

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_mac $dmac action drop

	$MZ $h1 -c 0 -p 100 -a own -b $dmac -t ip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $swp2

	# Allow packets to be flooded.
	ip link set dev $swp2 type bridge_slave flood on
	devlink_trap_action_set $trap_name "trap"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle when packets should not be dropped"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with when packets should not be dropped"

	tc_check_packets "dev $swp2 egress" 101 0
	check_fail $? "Packets not forwarded when should"

	devlink_trap_action_set $trap_name "drop"

	log_test "Port loopback filter - unicast"

	devlink_trap_drop_cleanup $mz_pid $swp2 ip
}

port_loopback_filter_test()
{
	port_loopback_filter_uc_test
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

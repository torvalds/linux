#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap ACL drops functionality over mlxsw.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	ingress_flow_action_drop_test
	egress_flow_action_drop_test
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

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

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

	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

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

ingress_flow_action_drop_test()
{
	local mz_pid

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower src_mac $h1mac action pass

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 \
		flower dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 0 -p 100 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -d 1msec -q &
	mz_pid=$!

	RET=0

	devlink_trap_drop_test ingress_flow_action_drop acl_drops $swp2 101

	log_test "ingress_flow_action_drop"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	devlink_trap_drop_cleanup $mz_pid $swp2 ip 1 101
}

egress_flow_action_drop_test()
{
	local mz_pid

	tc filter add dev $swp2 egress protocol ip pref 2 handle 102 \
		flower src_mac $h1mac action pass

	tc filter add dev $swp2 egress protocol ip pref 1 handle 101 \
		flower dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 0 -p 100 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -d 1msec -q &
	mz_pid=$!

	RET=0

	devlink_trap_drop_test egress_flow_action_drop acl_drops $swp2 102

	log_test "egress_flow_action_drop"

	tc filter del dev $swp2 egress protocol ip pref 1 handle 101 flower

	devlink_trap_drop_cleanup $mz_pid $swp2 ip 2 102
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

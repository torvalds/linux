#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	vlan_modify_ingress
	vlan_modify_egress
"

NUM_NETIFS=4
CHECK_TC="yes"
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28 2001:db8:1::1/64
	vlan_create $h1 85 v$h1 192.0.2.17/28 2001:db8:2::1/64
}

h1_destroy()
{
	vlan_destroy $h1 85
	simple_if_fini $h1 192.0.2.1/28 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28 2001:db8:1::2/64
	vlan_create $h2 65 v$h2 192.0.2.18/28 2001:db8:2::2/64
}

h2_destroy()
{
	vlan_destroy $h2 65
	simple_if_fini $h2 192.0.2.2/28 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge vlan_filtering 1 mcast_snooping 0

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	bridge vlan add dev $swp1 vid 85
	bridge vlan add dev $swp2 vid 65

	bridge vlan add dev $swp2 vid 85
	bridge vlan add dev $swp1 vid 65

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

	bridge vlan del vid 65 dev $swp1
	bridge vlan del vid 85 dev $swp2

	bridge vlan del vid 65 dev $swp2
	bridge vlan del vid 85 dev $swp1

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

vlan_modify_ingress()
{
	RET=0

	ping_do $h1.85 192.0.2.18
	check_fail $? "ping between two different vlans passed when should not"

	ping6_do $h1.85 2001:db8:2::2
	check_fail $? "ping6 between two different vlans passed when should not"

	tc filter add dev $swp1 ingress protocol all pref 1 handle 1 \
		flower action vlan modify id 65
	tc filter add dev $swp2 ingress protocol all pref 1 handle 1 \
		flower action vlan modify id 85

	ping_do $h1.85 192.0.2.18
	check_err $? "ping between two different vlans failed when should not"

	ping6_do $h1.85 2001:db8:2::2
	check_err $? "ping6 between two different vlans failed when should not"

	log_test "VLAN modify at ingress"

	tc filter del dev $swp2 ingress protocol all pref 1 handle 1 flower
	tc filter del dev $swp1 ingress protocol all pref 1 handle 1 flower
}

vlan_modify_egress()
{
	RET=0

	ping_do $h1.85 192.0.2.18
	check_fail $? "ping between two different vlans passed when should not"

	ping6_do $h1.85 2001:db8:2::2
	check_fail $? "ping6 between two different vlans passed when should not"

	tc filter add dev $swp1 egress protocol all pref 1 handle 1 \
		flower action vlan modify id 85
	tc filter add dev $swp2 egress protocol all pref 1 handle 1 \
		flower action vlan modify id 65

	ping_do $h1.85 192.0.2.18
	check_err $? "ping between two different vlans failed when should not"

	ping6_do $h1.85 2001:db8:2::2
	check_err $? "ping6 between two different vlans failed when should not"

	log_test "VLAN modify at egress"

	tc filter del dev $swp2 egress protocol all pref 1 handle 1 flower
	tc filter del dev $swp1 egress protocol all pref 1 handle 1 flower
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

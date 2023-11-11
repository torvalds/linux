#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="ping_ipv4 ping_ipv6 learning flooding vlan_deletion extern_learn"
NUM_NETIFS=4
CHECK_TC="yes"
source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 2001:db8:1::2/64
}

h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/24 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge \
		vlan_filtering 1 \
		ageing_time $LOW_AGEING_TIME \
		mcast_snooping 0

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

switch_destroy()
{
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

ping_ipv4()
{
	ping_test $h1 192.0.2.2
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:1::2
}

learning()
{
	learning_test "br0" $swp1 $h1 $h2
}

flooding()
{
	flood_test $swp2 $h1 $h2
}

vlan_deletion()
{
	# Test that the deletion of a VLAN on a bridge port does not affect
	# the PVID VLAN
	log_info "Add and delete a VLAN on bridge port $swp1"

	bridge vlan add vid 10 dev $swp1
	bridge vlan del vid 10 dev $swp1

	ping_ipv4
	ping_ipv6
}

extern_learn()
{
	local mac=de:ad:be:ef:13:37
	local ageing_time

	# Test that externally learned FDB entries can roam, but not age out
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev $swp1 master extern_learn vlan 1

	bridge fdb show brport $swp1 | grep -q de:ad:be:ef:13:37
	check_err $? "Did not find FDB entry when should"

	# Wait for 10 seconds after the ageing time to make sure the FDB entry
	# was not aged out
	ageing_time=$(bridge_ageing_time_get br0)
	sleep $((ageing_time + 10))

	bridge fdb show brport $swp1 | grep -q de:ad:be:ef:13:37
	check_err $? "FDB entry was aged out when should not"

	$MZ $h2 -c 1 -p 64 -a $mac -t ip -q

	bridge fdb show brport $swp2 | grep -q de:ad:be:ef:13:37
	check_err $? "FDB entry did not roam when should"

	log_test "Externally learned FDB entry - ageing & roaming"

	bridge fdb del de:ad:be:ef:13:37 dev $swp2 master vlan 1 &> /dev/null
	bridge fdb del de:ad:be:ef:13:37 dev $swp1 master vlan 1 &> /dev/null
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

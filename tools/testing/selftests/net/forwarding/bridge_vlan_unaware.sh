#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="ping_ipv4 ping_ipv6 learning flooding pvid_change"
NUM_NETIFS=4
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
	local msg=$1

	ping_test $h1 192.0.2.2 "$msg"
}

ping_ipv6()
{
	local msg=$1

	ping6_test $h1 2001:db8:1::2 "$msg"
}

learning()
{
	learning_test "br0" $swp1 $h1 $h2
}

flooding()
{
	flood_test $swp2 $h1 $h2
}

pvid_change()
{
	# Test that the changing of the VLAN-aware PVID does not affect
	# VLAN-unaware forwarding
	bridge vlan add vid 3 dev $swp1 pvid untagged

	ping_ipv4 " with bridge port $swp1 PVID changed"
	ping_ipv6 " with bridge port $swp1 PVID changed"

	bridge vlan del vid 3 dev $swp1

	ping_ipv4 " with bridge port $swp1 PVID deleted"
	ping_ipv6 " with bridge port $swp1 PVID deleted"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

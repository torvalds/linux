#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Verify that permanent mdb entries can be added to and deleted from bridge
# interfaces that are down, and works correctly when done so.

ALL_TESTS="add_del_to_port_down"
NUM_NETIFS=4

TEST_GROUP="239.10.10.10"
TEST_GROUP_MAC="01:00:5e:0a:0a:0a"

source lib.sh


add_del_to_port_down() {
	RET=0

	ip link set dev $swp2 down
	bridge mdb add dev br0 port "$swp2" grp $TEST_GROUP permanent 2>/dev/null
	check_err $? "Failed adding mdb entry"

	ip link set dev $swp2 up
	setup_wait_dev $swp2
	mcast_packet_test $TEST_GROUP_MAC 192.0.2.1 $TEST_GROUP $h1 $h2
	check_fail $? "Traffic to $TEST_GROUP wasn't forwarded"

	ip link set dev $swp2 down
	bridge mdb show dev br0 | grep -q "$TEST_GROUP permanent" 2>/dev/null
	check_err $? "MDB entry did not persist after link up/down"

	bridge mdb del dev br0 port "$swp2" grp $TEST_GROUP 2>/dev/null
	check_err $? "Failed deleting mdb entry"

	ip link set dev $swp2 up
	setup_wait_dev $swp2
	mcast_packet_test $TEST_GROUP_MAC 192.0.2.1 $TEST_GROUP $h1 $h2
	check_err $? "Traffic to $TEST_GROUP was forwarded after entry removed"

	log_test "MDB add/del entry to port with state down "
}

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
	# Enable multicast filtering
	ip link add dev br0 type bridge mcast_snooping 1 mcast_querier 1

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up

	bridge link set dev $swp2 mcast_flood off
	# Bridge currently has a "grace time" at creation time before it
	# forwards multicast according to the mdb. Since we disable the
	# mcast_flood setting per port
	sleep 10
}

switch_destroy()
{
	ip link set dev $swp1 down
	ip link set dev $swp2 down
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
	h1_destroy
	h2_destroy

	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
tests_run
exit $EXIT_STATUS

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Verify that adding host mdb entries work as intended for all types of
# multicast filters: ipv4, ipv6, and mac

ALL_TESTS="mdb_add_del_test"
NUM_NETIFS=2

TEST_GROUP_IP4="225.1.2.3"
TEST_GROUP_IP6="ff02::42"
TEST_GROUP_MAC="01:00:01:c0:ff:ee"

source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

switch_create()
{
	# Enable multicast filtering
	ip link add dev br0 type bridge mcast_snooping 1

	ip link set dev $swp1 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
}

switch_destroy()
{
	ip link set dev $swp1 down
	ip link del dev br0
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

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

do_mdb_add_del()
{
	local group=$1
	local flag=$2

	RET=0
	bridge mdb add dev br0 port br0 grp $group $flag 2>/dev/null
	check_err $? "Failed adding $group to br0, port br0"

	if [ -z "$flag" ]; then
	    flag="temp"
	fi

	bridge mdb show dev br0 | grep $group | grep -q $flag 2>/dev/null
	check_err $? "$group not added with $flag flag"

	bridge mdb del dev br0 port br0 grp $group 2>/dev/null
	check_err $? "Failed deleting $group from br0, port br0"

	bridge mdb show dev br0 | grep -q $group >/dev/null
	check_err_fail 1 $? "$group still in mdb after delete"

	log_test "MDB add/del group $group to bridge port br0"
}

mdb_add_del_test()
{
	do_mdb_add_del $TEST_GROUP_MAC permanent
	do_mdb_add_del $TEST_GROUP_IP4
	do_mdb_add_del $TEST_GROUP_IP6
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

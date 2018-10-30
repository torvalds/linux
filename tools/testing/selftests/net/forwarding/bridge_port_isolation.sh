#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="ping_ipv4 ping_ipv6 flooding"
NUM_NETIFS=6
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

h3_create()
{
	simple_if_init $h3 192.0.2.3/24 2001:db8:1::3/64
}

h3_destroy()
{
	simple_if_fini $h3 192.0.2.3/24 2001:db8:1::3/64
}

switch_create()
{
	ip link add dev br0 type bridge

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0
	ip link set dev $swp3 master br0

	ip link set dev $swp1 type bridge_slave isolated on
	check_err $? "Can't set isolation on port $swp1"
	ip link set dev $swp2 type bridge_slave isolated on
	check_err $? "Can't set isolation on port $swp2"
	ip link set dev $swp3 type bridge_slave isolated off
	check_err $? "Can't disable isolation on port $swp3"

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up
	ip link set dev $swp3 up
}

switch_destroy()
{
	ip link set dev $swp3 down
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

	swp3=${NETIFS[p5]}
	h3=${NETIFS[p6]}

	vrf_prepare

	h1_create
	h2_create
	h3_create

	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy

	h3_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	RET=0
	ping_do $h1 192.0.2.2
	check_fail $? "Ping worked when it should not have"

	RET=0
	ping_do $h3 192.0.2.2
	check_err $? "Ping didn't work when it should have"

	log_test "Isolated port ping"
}

ping_ipv6()
{
	RET=0
	ping6_do $h1 2001:db8:1::2
	check_fail $? "Ping6 worked when it should not have"

	RET=0
	ping6_do $h3 2001:db8:1::2
	check_err $? "Ping6 didn't work when it should have"

	log_test "Isolated port ping6"
}

flooding()
{
	local mac=de:ad:be:ef:13:37
	local ip=192.0.2.100

	RET=0
	flood_test_do false $mac $ip $h1 $h2
	check_err $? "Packet was flooded when it should not have been"

	RET=0
	flood_test_do true $mac $ip $h3 $h2
	check_err $? "Packet was not flooded when it should have been"

	log_test "Isolated port flooding"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="vlmc_control_test"
NUM_NETIFS=4
CHECK_TC="yes"
TEST_GROUP="239.10.10.10"

source lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64
	ip link add l $h1 $h1.10 up type vlan id 10
}

h1_destroy()
{
	ip link del $h1.10
	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 2001:db8:1::2/64
	ip link add l $h2 $h2.10 up type vlan id 10
}

h2_destroy()
{
	ip link del $h2.10
	simple_if_fini $h2 192.0.2.2/24 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge mcast_snooping 1 mcast_querier 1 vlan_filtering 1

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	bridge vlan add vid 10-11 dev $swp1 master
	bridge vlan add vid 10-11 dev $swp2 master

	ip link set dev br0 type bridge mcast_vlan_snooping 1
	check_err $? "Could not enable global vlan multicast snooping"
	log_test "Vlan multicast snooping enable"
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

vlmc_v2join_test()
{
	local expect=$1

	RET=0
	ip address add dev $h2.10 $TEST_GROUP/32 autojoin
	check_err $? "Could not join $TEST_GROUP"

	sleep 5
	bridge -j mdb show dev br0 |
		jq -e ".[].mdb[] | select(.grp == \"$TEST_GROUP\" and .vid == 10)" &>/dev/null
	if [ $expect -eq 0 ]; then
		check_err $? "IGMPv2 report didn't create mdb entry for $TEST_GROUP"
	else
		check_fail $? "IGMPv2 report shouldn't have created mdb entry for $TEST_GROUP"
	fi

	# check if we need to cleanup
	if [ $RET -eq 0 ]; then
		ip address del dev $h2.10 $TEST_GROUP/32 2>&1 1>/dev/null
		sleep 5
		bridge -j mdb show dev br0 |
			jq -e ".[].mdb[] | select(.grp == \"$TEST_GROUP\" and \
						  .vid == 10)" &>/dev/null
		check_fail $? "IGMPv2 leave didn't remove mdb entry for $TEST_GROUP"
	fi
}

vlmc_control_test()
{
	RET=0
	local goutput=`bridge -j vlan global show`
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10)" &>/dev/null
	check_err $? "Could not find vlan 10's global options"
	log_test "Vlan global options existence"

	RET=0
	echo -n $goutput |
		jq -e ".[].vlans[] | select(.vlan == 10 and .mcast_snooping == 1) " &>/dev/null
	check_err $? "Wrong default mcast_snooping global option value"
	log_test "Vlan mcast_snooping global option default value"

	RET=0
	vlmc_v2join_test 0
	bridge vlan global set vid 10 dev br0 mcast_snooping 0
	check_err $? "Could not disable multicast snooping in vlan 10"
	vlmc_v2join_test 1
	log_test "Vlan 10 multicast snooping control"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

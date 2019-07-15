#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="reportleave_test"
NUM_NETIFS=4
CHECK_TC="yes"
TEST_GROUP="239.10.10.10"
TEST_GROUP_MAC="01:00:5e:0a:0a:0a"
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
	ip link add dev br0 type bridge mcast_snooping 1 mcast_querier 1

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

	# Always cleanup the mcast group
	ip address del dev $h2 $TEST_GROUP/32 2>&1 1>/dev/null

	h2_destroy
	h1_destroy

	vrf_cleanup
}

# return 0 if the packet wasn't seen on host2_if or 1 if it was
mcast_packet_test()
{
	local mac=$1
	local ip=$2
	local host1_if=$3
	local host2_if=$4
	local seen=0

	# Add an ACL on `host2_if` which will tell us whether the packet
	# was received by it or not.
	tc qdisc add dev $host2_if ingress
	tc filter add dev $host2_if ingress protocol ip pref 1 handle 101 \
		flower dst_mac $mac action drop

	$MZ $host1_if -c 1 -p 64 -b $mac -B $ip -t udp "dp=4096,sp=2048" -q
	sleep 1

	tc -j -s filter show dev $host2_if ingress \
		| jq -e ".[] | select(.options.handle == 101) \
		| select(.options.actions[0].stats.packets == 1)" &> /dev/null
	if [[ $? -eq 0 ]]; then
		seen=1
	fi

	tc filter del dev $host2_if ingress protocol ip pref 1 handle 101 flower
	tc qdisc del dev $host2_if ingress

	return $seen
}

reportleave_test()
{
	RET=0
	ip address add dev $h2 $TEST_GROUP/32 autojoin
	check_err $? "Could not join $TEST_GROUP"

	sleep 5
	bridge mdb show dev br0 | grep $TEST_GROUP 1>/dev/null
	check_err $? "Report didn't create mdb entry for $TEST_GROUP"

	mcast_packet_test $TEST_GROUP_MAC $TEST_GROUP $h1 $h2
	check_fail $? "Traffic to $TEST_GROUP wasn't forwarded"

	log_test "IGMP report $TEST_GROUP"

	RET=0
	bridge mdb show dev br0 | grep $TEST_GROUP 1>/dev/null
	check_err $? "mdb entry for $TEST_GROUP is missing"

	ip address del dev $h2 $TEST_GROUP/32
	check_err $? "Could not leave $TEST_GROUP"

	sleep 5
	bridge mdb show dev br0 | grep $TEST_GROUP 1>/dev/null
	check_fail $? "Leave didn't delete mdb entry for $TEST_GROUP"

	mcast_packet_test $TEST_GROUP_MAC $TEST_GROUP $h1 $h2
	check_err $? "Traffic to $TEST_GROUP was forwarded without mdb entry"

	log_test "IGMP leave $TEST_GROUP"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

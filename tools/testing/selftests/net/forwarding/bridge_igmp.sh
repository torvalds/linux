#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="v2reportleave_test v3include_test v3inc_allow_test v3inc_is_include_test \
	   v3inc_is_exclude_test v3inc_to_exclude_test v3exc_allow_test v3exc_is_include_test \
	   v3exc_is_exclude_test v3exc_to_exclude_test v3inc_block_test v3exc_block_test \
	   v3exc_timeout_test v3star_ex_auto_add_test"
NUM_NETIFS=4
CHECK_TC="yes"
TEST_GROUP="239.10.10.10"
TEST_GROUP_MAC="01:00:5e:0a:0a:0a"

ALL_GROUP="224.0.0.1"
ALL_MAC="01:00:5e:00:00:01"

# IGMPv3 is_in report: grp 239.10.10.10 is_include 192.0.2.1,192.0.2.2,192.0.2.3
MZPKT_IS_INC="22:00:9d:de:00:00:00:01:01:00:00:03:ef:0a:0a:0a:c0:00:02:01:c0:00:02:02:c0:00:02:03"
# IGMPv3 is_in report: grp 239.10.10.10 is_include 192.0.2.10,192.0.2.11,192.0.2.12
MZPKT_IS_INC2="22:00:9d:c3:00:00:00:01:01:00:00:03:ef:0a:0a:0a:c0:00:02:0a:c0:00:02:0b:c0:00:02:0c"
# IGMPv3 is_in report: grp 239.10.10.10 is_include 192.0.2.20,192.0.2.30
MZPKT_IS_INC3="22:00:5f:b4:00:00:00:01:01:00:00:02:ef:0a:0a:0a:c0:00:02:14:c0:00:02:1e"
# IGMPv3 allow report: grp 239.10.10.10 allow 192.0.2.10,192.0.2.11,192.0.2.12
MZPKT_ALLOW="22:00:99:c3:00:00:00:01:05:00:00:03:ef:0a:0a:0a:c0:00:02:0a:c0:00:02:0b:c0:00:02:0c"
# IGMPv3 allow report: grp 239.10.10.10 allow 192.0.2.20,192.0.2.30
MZPKT_ALLOW2="22:00:5b:b4:00:00:00:01:05:00:00:02:ef:0a:0a:0a:c0:00:02:14:c0:00:02:1e"
# IGMPv3 is_ex report: grp 239.10.10.10 is_exclude 192.0.2.1,192.0.2.2,192.0.2.20,192.0.2.21
MZPKT_IS_EXC="22:00:da:b6:00:00:00:01:02:00:00:04:ef:0a:0a:0a:c0:00:02:01:c0:00:02:02:c0:00:02:14:c0:00:02:15"
# IGMPv3 is_ex report: grp 239.10.10.10 is_exclude 192.0.2.20,192.0.2.30
MZPKT_IS_EXC2="22:00:5e:b4:00:00:00:01:02:00:00:02:ef:0a:0a:0a:c0:00:02:14:c0:00:02:1e"
# IGMPv3 to_ex report: grp 239.10.10.10 to_exclude 192.0.2.1,192.0.2.20,192.0.2.30
MZPKT_TO_EXC="22:00:9a:b1:00:00:00:01:04:00:00:03:ef:0a:0a:0a:c0:00:02:01:c0:00:02:14:c0:00:02:1e"
# IGMPv3 block report: grp 239.10.10.10 block 192.0.2.1,192.0.2.20,192.0.2.30
MZPKT_BLOCK="22:00:98:b1:00:00:00:01:06:00:00:03:ef:0a:0a:0a:c0:00:02:01:c0:00:02:14:c0:00:02:1e"

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

v2reportleave_test()
{
	RET=0
	ip address add dev $h2 $TEST_GROUP/32 autojoin
	check_err $? "Could not join $TEST_GROUP"

	sleep 5
	bridge mdb show dev br0 | grep $TEST_GROUP 1>/dev/null
	check_err $? "IGMPv2 report didn't create mdb entry for $TEST_GROUP"

	mcast_packet_test $TEST_GROUP_MAC 192.0.2.1 $TEST_GROUP $h1 $h2
	check_fail $? "Traffic to $TEST_GROUP wasn't forwarded"

	log_test "IGMPv2 report $TEST_GROUP"

	RET=0
	bridge mdb show dev br0 | grep $TEST_GROUP 1>/dev/null
	check_err $? "mdb entry for $TEST_GROUP is missing"

	ip address del dev $h2 $TEST_GROUP/32
	check_err $? "Could not leave $TEST_GROUP"

	sleep 5
	bridge mdb show dev br0 | grep $TEST_GROUP 1>/dev/null
	check_fail $? "Leave didn't delete mdb entry for $TEST_GROUP"

	mcast_packet_test $TEST_GROUP_MAC 192.0.2.1 $TEST_GROUP $h1 $h2
	check_err $? "Traffic to $TEST_GROUP was forwarded without mdb entry"

	log_test "IGMPv2 leave $TEST_GROUP"
}

v3include_prepare()
{
	local host1_if=$1
	local mac=$2
	local group=$3
	local X=("192.0.2.1" "192.0.2.2" "192.0.2.3")

	ip link set dev br0 type bridge mcast_igmp_version 3
	check_err $? "Could not change bridge IGMP version to 3"

	$MZ $host1_if -b $mac -c 1 -B $group -t ip "proto=2,p=$MZPKT_IS_INC" -q
	sleep 1
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and .source_list != null)" &>/dev/null
	check_err $? "Missing *,G entry with source list"
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and .filter_mode == \"include\")" &>/dev/null
	check_err $? "Wrong *,G entry filter mode"
	brmcast_check_sg_entries "is_include" "${X[@]}"
}

v3exclude_prepare()
{
	local host1_if=$1
	local mac=$2
	local group=$3
	local pkt=$4
	local X=("192.0.2.1" "192.0.2.2")
	local Y=("192.0.2.20" "192.0.2.21")

	v3include_prepare $host1_if $mac $group

	$MZ $host1_if -c 1 -b $mac -B $group -t ip "proto=2,p=$MZPKT_IS_EXC" -q
	sleep 1
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and .filter_mode == \"exclude\")" &>/dev/null
	check_err $? "Wrong *,G entry filter mode"

	brmcast_check_sg_entries "is_exclude" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"192.0.2.3\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 192.0.2.3 entry still exists"
}

v3cleanup()
{
	local port=$1
	local group=$2

	bridge mdb del dev br0 port $port grp $group
	ip link set dev br0 type bridge mcast_igmp_version 2
}

v3include_test()
{
	RET=0
	local X=("192.0.2.1" "192.0.2.2" "192.0.2.3")

	v3include_prepare $h1 $ALL_MAC $ALL_GROUP

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "192.0.2.100"

	log_test "IGMPv3 report $TEST_GROUP is_include"

	v3cleanup $swp1 $TEST_GROUP
}

v3inc_allow_test()
{
	RET=0
	local X=("192.0.2.10" "192.0.2.11" "192.0.2.12")

	v3include_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_ALLOW" -q
	sleep 1
	brmcast_check_sg_entries "allow" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "192.0.2.100"

	log_test "IGMPv3 report $TEST_GROUP include -> allow"

	v3cleanup $swp1 $TEST_GROUP
}

v3inc_is_include_test()
{
	RET=0
	local X=("192.0.2.10" "192.0.2.11" "192.0.2.12")

	v3include_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_IS_INC2" -q
	sleep 1
	brmcast_check_sg_entries "is_include" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "192.0.2.100"

	log_test "IGMPv3 report $TEST_GROUP include -> is_include"

	v3cleanup $swp1 $TEST_GROUP
}

v3inc_is_exclude_test()
{
	RET=0

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP include -> is_exclude"

	v3cleanup $swp1 $TEST_GROUP
}

v3inc_to_exclude_test()
{
	RET=0
	local X=("192.0.2.1")
	local Y=("192.0.2.20" "192.0.2.30")

	v3include_prepare $h1 $ALL_MAC $ALL_GROUP

	ip link set dev br0 type bridge mcast_last_member_interval 500
	check_err $? "Could not change mcast_last_member_interval to 5s"

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_TO_EXC" -q
	sleep 1
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and .filter_mode == \"exclude\")" &>/dev/null
	check_err $? "Wrong *,G entry filter mode"

	brmcast_check_sg_entries "to_exclude" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"192.0.2.2\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 192.0.2.2 entry still exists"
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"192.0.2.21\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 192.0.2.21 entry still exists"

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP include -> to_exclude"

	ip link set dev br0 type bridge mcast_last_member_interval 100

	v3cleanup $swp1 $TEST_GROUP
}

v3exc_allow_test()
{
	RET=0
	local X=("192.0.2.1" "192.0.2.2" "192.0.2.20" "192.0.2.30")
	local Y=("192.0.2.21")

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_ALLOW2" -q
	sleep 1
	brmcast_check_sg_entries "allow" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP exclude -> allow"

	v3cleanup $swp1 $TEST_GROUP
}

v3exc_is_include_test()
{
	RET=0
	local X=("192.0.2.1" "192.0.2.2" "192.0.2.20" "192.0.2.30")
	local Y=("192.0.2.21")

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_IS_INC3" -q
	sleep 1
	brmcast_check_sg_entries "is_include" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP exclude -> is_include"

	v3cleanup $swp1 $TEST_GROUP
}

v3exc_is_exclude_test()
{
	RET=0
	local X=("192.0.2.30")
	local Y=("192.0.2.20")

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_IS_EXC2" -q
	sleep 1
	brmcast_check_sg_entries "is_exclude" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP exclude -> is_exclude"

	v3cleanup $swp1 $TEST_GROUP
}

v3exc_to_exclude_test()
{
	RET=0
	local X=("192.0.2.1" "192.0.2.30")
	local Y=("192.0.2.20")

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	ip link set dev br0 type bridge mcast_last_member_interval 500
	check_err $? "Could not change mcast_last_member_interval to 5s"

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_TO_EXC" -q
	sleep 1
	brmcast_check_sg_entries "to_exclude" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP exclude -> to_exclude"

	ip link set dev br0 type bridge mcast_last_member_interval 100

	v3cleanup $swp1 $TEST_GROUP
}

v3inc_block_test()
{
	RET=0
	local X=("192.0.2.2" "192.0.2.3")

	v3include_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_BLOCK" -q
	# make sure the lowered timers have expired (by default 2 seconds)
	sleep 3
	brmcast_check_sg_entries "block" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"192.0.2.1\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 192.0.2.1 entry still exists"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "192.0.2.100"

	log_test "IGMPv3 report $TEST_GROUP include -> block"

	v3cleanup $swp1 $TEST_GROUP
}

v3exc_block_test()
{
	RET=0
	local X=("192.0.2.1" "192.0.2.2" "192.0.2.30")
	local Y=("192.0.2.20" "192.0.2.21")

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	ip link set dev br0 type bridge mcast_last_member_interval 500
	check_err $? "Could not change mcast_last_member_interval to 5s"

	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_BLOCK" -q
	sleep 1
	brmcast_check_sg_entries "block" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 192.0.2.100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "IGMPv3 report $TEST_GROUP exclude -> block"

	ip link set dev br0 type bridge mcast_last_member_interval 100

	v3cleanup $swp1 $TEST_GROUP
}

v3exc_timeout_test()
{
	RET=0
	local X=("192.0.2.20" "192.0.2.30")

	# GMI should be 3 seconds
	ip link set dev br0 type bridge mcast_query_interval 100 mcast_query_response_interval 100

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP
	ip link set dev br0 type bridge mcast_query_interval 500 mcast_query_response_interval 500
	$MZ $h1 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_ALLOW2" -q
	sleep 3
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and .filter_mode == \"include\")" &>/dev/null
	check_err $? "Wrong *,G entry filter mode"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"192.0.2.1\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 192.0.2.1 entry still exists"
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"192.0.2.2\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 192.0.2.2 entry still exists"

	brmcast_check_sg_entries "allow" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 192.0.2.100

	log_test "IGMPv3 group $TEST_GROUP exclude timeout"

	ip link set dev br0 type bridge mcast_query_interval 12500 \
					mcast_query_response_interval 1000

	v3cleanup $swp1 $TEST_GROUP
}

v3star_ex_auto_add_test()
{
	RET=0

	v3exclude_prepare $h1 $ALL_MAC $ALL_GROUP

	$MZ $h2 -c 1 -b $ALL_MAC -B $ALL_GROUP -t ip "proto=2,p=$MZPKT_IS_INC" -q
	sleep 1
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and .src == \"192.0.2.3\" and \
				.port == \"$swp1\")" &>/dev/null
	check_err $? "S,G entry for *,G port doesn't exist"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and .src == \"192.0.2.3\" and \
				.port == \"$swp1\" and \
				.flags[] == \"added_by_star_ex\")" &>/dev/null
	check_err $? "Auto-added S,G entry doesn't have added_by_star_ex flag"

	brmcast_check_sg_fwding 1 192.0.2.3

	log_test "IGMPv3 S,G port entry automatic add to a *,G port"

	v3cleanup $swp1 $TEST_GROUP
	v3cleanup $swp2 $TEST_GROUP
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

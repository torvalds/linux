#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	mldv2include_test
	mldv2inc_allow_test
	mldv2inc_is_include_test
	mldv2inc_is_exclude_test
	mldv2inc_to_exclude_test
	mldv2exc_allow_test
	mldv2exc_is_include_test
	mldv2exc_is_exclude_test
	mldv2exc_to_exclude_test
	mldv2inc_block_test
	mldv2exc_block_test
	mldv2exc_timeout_test
	mldv2star_ex_auto_add_test
	mldv2per_vlan_snooping_port_stp_test
	mldv2per_vlan_snooping_vlan_stp_test
"
NUM_NETIFS=4
CHECK_TC="yes"
TEST_GROUP="ff02::cc"
TEST_GROUP_MAC="33:33:00:00:00:cc"

# MLDv2 is_in report: grp ff02::cc is_include 2001:db8:1::1,2001:db8:1::2,2001:db8:1::3
MZPKT_IS_INC="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:\
00:00:00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:\
00:05:02:00:00:00:00:8f:00:8e:d9:00:00:00:01:01:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:\
00:00:00:00:cc:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01:20:01:0d:b8:00:01:00:00:00:\
00:00:00:00:00:00:02:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:03"
# MLDv2 is_in report: grp ff02::cc is_include 2001:db8:1::10,2001:db8:1::11,2001:db8:1::12
MZPKT_IS_INC2="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:\
00:00:00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:\
05:02:00:00:00:00:8f:00:8e:ac:00:00:00:01:01:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:00:00:\
00:00:cc:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:10:20:01:0d:b8:00:01:00:00:00:00:00:00:\
00:00:00:11:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:12"
# MLDv2 is_in report: grp ff02::cc is_include 2001:db8:1::20,2001:db8:1::30
MZPKT_IS_INC3="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:44:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:bc:5a:00:00:00:01:01:00:00:02:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:30"
# MLDv2 allow report: grp ff02::cc allow 2001:db8:1::10,2001:db8:1::11,2001:db8:1::12
MZPKT_ALLOW="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:00:\
00:00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:\
02:00:00:00:00:8f:00:8a:ac:00:00:00:01:05:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:\
00:cc:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:10:20:01:0d:b8:00:01:00:00:00:00:00:00:00:\
00:00:11:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:12"
# MLDv2 allow report: grp ff02::cc allow 2001:db8:1::20,2001:db8:1::30
MZPKT_ALLOW2="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:44:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:b8:5a:00:00:00:01:05:00:00:02:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:30"
# MLDv2 is_ex report: grp ff02::cc is_exclude 2001:db8:1::1,2001:db8:1::2,2001:db8:1::20,2001:db8:1::21
MZPKT_IS_EXC="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:64:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:5f:d0:00:00:00:01:02:00:00:04:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:02:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:21"
# MLDv2 is_ex report: grp ff02::cc is_exclude 2001:db8:1::20,2001:db8:1::30
MZPKT_IS_EXC2="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:44:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:bb:5a:00:00:00:01:02:00:00:02:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:30"
# MLDv2 to_ex report: grp ff02::cc to_exclude 2001:db8:1::1,2001:db8:1::20,2001:db8:1::30
MZPKT_TO_EXC="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:8b:8e:00:00:00:01:04:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:30"
# MLDv2 block report: grp ff02::cc block 2001:db8:1::1,2001:db8:1::20,2001:db8:1::30
MZPKT_BLOCK="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:00:00:00:\
00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:00:\
00:00:8f:00:89:8e:00:00:00:01:06:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:01:\
0d:b8:00:01:00:00:00:00:00:00:00:00:00:01:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:01:\
0d:b8:00:01:00:00:00:00:00:00:00:00:00:30"

source lib.sh

h1_create()
{
	simple_if_init $h1 2001:db8:1::1/64
}

h1_destroy()
{
	simple_if_fini $h1 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 2001:db8:1::2/64
}

h2_destroy()
{
	simple_if_fini $h2 2001:db8:1::2/64
}

switch_create()
{
	ip link add dev br0 type bridge mcast_snooping 1 mcast_query_response_interval 100 \
					mcast_mld_version 2 mcast_startup_query_interval 300 \
					mcast_querier 1

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev $swp2 up

	# make sure a query has been generated
	sleep 5
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

mldv2include_prepare()
{
	local host1_if=$1
	local X=("2001:db8:1::1" "2001:db8:1::2" "2001:db8:1::3")

	ip link set dev br0 type bridge mcast_mld_version 2
	check_err $? "Could not change bridge MLD version to 2"

	$MZ $host1_if $MZPKT_IS_INC -q
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

mldv2exclude_prepare()
{
	local host1_if=$1
	local mac=$2
	local group=$3
	local pkt=$4
	local X=("2001:db8:1::1" "2001:db8:1::2")
	local Y=("2001:db8:1::20" "2001:db8:1::21")

	mldv2include_prepare $h1

	$MZ $host1_if -c 1 $MZPKT_IS_EXC -q
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
				.source_list[].address == \"2001:db8:1::3\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 2001:db8:1::3 entry still exists"
}

mldv2cleanup()
{
	local port=$1

	bridge mdb del dev br0 port $port grp $TEST_GROUP
	ip link set dev br0 type bridge mcast_mld_version 1
}

mldv2include_test()
{
	RET=0
	local X=("2001:db8:1::1" "2001:db8:1::2" "2001:db8:1::3")

	mldv2include_prepare $h1

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "2001:db8:1::100"

	log_test "MLDv2 report $TEST_GROUP is_include"

	mldv2cleanup $swp1
}

mldv2inc_allow_test()
{
	RET=0
	local X=("2001:db8:1::10" "2001:db8:1::11" "2001:db8:1::12")

	mldv2include_prepare $h1

	$MZ $h1 -c 1 $MZPKT_ALLOW -q
	sleep 1
	brmcast_check_sg_entries "allow" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "2001:db8:1::100"

	log_test "MLDv2 report $TEST_GROUP include -> allow"

	mldv2cleanup $swp1
}

mldv2inc_is_include_test()
{
	RET=0
	local X=("2001:db8:1::10" "2001:db8:1::11" "2001:db8:1::12")

	mldv2include_prepare $h1

	$MZ $h1 -c 1 $MZPKT_IS_INC2 -q
	sleep 1
	brmcast_check_sg_entries "is_include" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 "2001:db8:1::100"

	log_test "MLDv2 report $TEST_GROUP include -> is_include"

	mldv2cleanup $swp1
}

mldv2inc_is_exclude_test()
{
	RET=0

	mldv2exclude_prepare $h1

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP include -> is_exclude"

	mldv2cleanup $swp1
}

mldv2inc_to_exclude_test()
{
	RET=0
	local X=("2001:db8:1::1")
	local Y=("2001:db8:1::20" "2001:db8:1::30")

	mldv2include_prepare $h1

	ip link set dev br0 type bridge mcast_last_member_interval 500
	check_err $? "Could not change mcast_last_member_interval to 5s"

	$MZ $h1 -c 1 $MZPKT_TO_EXC -q
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
				.source_list[].address == \"2001:db8:1::2\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 2001:db8:1::2 entry still exists"
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"2001:db8:1::21\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 2001:db8:1::21 entry still exists"

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP include -> to_exclude"

	ip link set dev br0 type bridge mcast_last_member_interval 100

	mldv2cleanup $swp1
}

mldv2exc_allow_test()
{
	RET=0
	local X=("2001:db8:1::1" "2001:db8:1::2" "2001:db8:1::20" "2001:db8:1::30")
	local Y=("2001:db8:1::21")

	mldv2exclude_prepare $h1

	$MZ $h1 -c 1 $MZPKT_ALLOW2 -q
	sleep 1
	brmcast_check_sg_entries "allow" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP exclude -> allow"

	mldv2cleanup $swp1
}

mldv2exc_is_include_test()
{
	RET=0
	local X=("2001:db8:1::1" "2001:db8:1::2" "2001:db8:1::20" "2001:db8:1::30")
	local Y=("2001:db8:1::21")

	mldv2exclude_prepare $h1

	$MZ $h1 -c 1 $MZPKT_IS_INC3 -q
	sleep 1
	brmcast_check_sg_entries "is_include" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP exclude -> is_include"

	mldv2cleanup $swp1
}

mldv2exc_is_exclude_test()
{
	RET=0
	local X=("2001:db8:1::30")
	local Y=("2001:db8:1::20")

	mldv2exclude_prepare $h1

	$MZ $h1 -c 1 $MZPKT_IS_EXC2 -q
	sleep 1
	brmcast_check_sg_entries "is_exclude" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP exclude -> is_exclude"

	mldv2cleanup $swp1
}

mldv2exc_to_exclude_test()
{
	RET=0
	local X=("2001:db8:1::1" "2001:db8:1::30")
	local Y=("2001:db8:1::20")

	mldv2exclude_prepare $h1

	ip link set dev br0 type bridge mcast_last_member_interval 500
	check_err $? "Could not change mcast_last_member_interval to 5s"

	$MZ $h1 -c 1 $MZPKT_TO_EXC -q
	sleep 1
	brmcast_check_sg_entries "to_exclude" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP exclude -> to_exclude"

	ip link set dev br0 type bridge mcast_last_member_interval 100

	mldv2cleanup $swp1
}

mldv2inc_block_test()
{
	RET=0
	local X=("2001:db8:1::2" "2001:db8:1::3")

	mldv2include_prepare $h1

	$MZ $h1 -c 1 $MZPKT_BLOCK -q
	# make sure the lowered timers have expired (by default 2 seconds)
	sleep 3
	brmcast_check_sg_entries "block" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"2001:db8:1::1\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 2001:db8:1::1 entry still exists"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 2001:db8:1::100

	log_test "MLDv2 report $TEST_GROUP include -> block"

	mldv2cleanup $swp1
}

mldv2exc_block_test()
{
	RET=0
	local X=("2001:db8:1::1" "2001:db8:1::2" "2001:db8:1::30")
	local Y=("2001:db8:1::20" "2001:db8:1::21")

	mldv2exclude_prepare $h1

	ip link set dev br0 type bridge mcast_last_member_interval 500
	check_err $? "Could not change mcast_last_member_interval to 5s"

	$MZ $h1 -c 1 $MZPKT_BLOCK -q
	sleep 1
	brmcast_check_sg_entries "block" "${X[@]}" "${Y[@]}"

	brmcast_check_sg_state 0 "${X[@]}"
	brmcast_check_sg_state 1 "${Y[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}" 2001:db8:1::100
	brmcast_check_sg_fwding 0 "${Y[@]}"

	log_test "MLDv2 report $TEST_GROUP exclude -> block"

	ip link set dev br0 type bridge mcast_last_member_interval 100

	mldv2cleanup $swp1
}

mldv2exc_timeout_test()
{
	RET=0
	local X=("2001:db8:1::20" "2001:db8:1::30")

	# GMI should be 5 seconds
	ip link set dev br0 type bridge mcast_query_interval 100 \
					mcast_query_response_interval 100 \
					mcast_membership_interval 500

	mldv2exclude_prepare $h1
	ip link set dev br0 type bridge mcast_query_interval 500 \
					mcast_query_response_interval 500 \
					mcast_membership_interval 1500

	$MZ $h1 -c 1 $MZPKT_ALLOW2 -q
	sleep 5
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and .filter_mode == \"include\")" &>/dev/null
	check_err $? "Wrong *,G entry filter mode"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"2001:db8:1::1\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 2001:db8:1::1 entry still exists"
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and \
				.source_list != null and
				.source_list[].address == \"2001:db8:1::2\")" &>/dev/null
	check_fail $? "Wrong *,G entry source list, 2001:db8:1::2 entry still exists"

	brmcast_check_sg_entries "allow" "${X[@]}"

	brmcast_check_sg_state 0 "${X[@]}"

	brmcast_check_sg_fwding 1 "${X[@]}"
	brmcast_check_sg_fwding 0 2001:db8:1::100

	log_test "MLDv2 group $TEST_GROUP exclude timeout"

	ip link set dev br0 type bridge mcast_query_interval 12500 \
					mcast_query_response_interval 1000 \
					mcast_membership_interval 26000

	mldv2cleanup $swp1
}

mldv2star_ex_auto_add_test()
{
	RET=0

	mldv2exclude_prepare $h1

	$MZ $h2 -c 1 $MZPKT_IS_INC -q
	sleep 1
	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and .src == \"2001:db8:1::3\" and \
				.port == \"$swp1\")" &>/dev/null
	check_err $? "S,G entry for *,G port doesn't exist"

	bridge -j -d -s mdb show dev br0 \
		| jq -e ".[].mdb[] | \
			 select(.grp == \"$TEST_GROUP\" and .src == \"2001:db8:1::3\" and \
				.port == \"$swp1\" and \
				.flags[] == \"added_by_star_ex\")" &>/dev/null
	check_err $? "Auto-added S,G entry doesn't have added_by_star_ex flag"

	brmcast_check_sg_fwding 1 2001:db8:1::3

	log_test "MLDv2 S,G port entry automatic add to a *,G port"

	mldv2cleanup $swp1
	mldv2cleanup $swp2
}

mldv2per_vlan_snooping_stp_test()
{
	local is_port=$1

	local msg="port"
	[[ $is_port -ne 1 ]] && msg="vlan"

	ip link set br0 up type bridge vlan_filtering 1 \
					mcast_mld_version 2 \
					mcast_snooping 1 \
					mcast_vlan_snooping 1 \
					mcast_querier 1 \
					mcast_stats_enabled 1
	bridge vlan global set vid 1 dev br0 \
					mcast_mld_version 2 \
					mcast_snooping 1 \
					mcast_querier 1 \
					mcast_query_interval 100 \
					mcast_startup_query_count 0

	[[ $is_port -eq 1 ]] && bridge link set dev $swp1 state 0
	[[ $is_port -ne 1 ]] && bridge vlan set vid 1 dev $swp1 state 4
	sleep 5
	local tx_s=$(ip -j -p stats show dev $swp1 \
			group xstats_slave subgroup bridge suite mcast \
			| jq '.[]["multicast"]["mld_queries"]["tx_v2"]')
	[[ $is_port -eq 1 ]] && bridge link set dev $swp1 state 3
	[[ $is_port -ne 1 ]] && bridge vlan set vid 1 dev $swp1 state 3
	sleep 5
	local tx_e=$(ip -j -p stats show dev $swp1 \
			group xstats_slave subgroup bridge suite mcast \
			| jq '.[]["multicast"]["mld_queries"]["tx_v2"]')

	RET=0
	local tx=$(expr $tx_e - $tx_s)
	test $tx -gt 0
	check_err $? "No MLD queries after STP state becomes forwarding"
	log_test "per vlan snooping with $msg stp state change"

	# restore settings
	bridge vlan global set vid 1 dev br0 \
					mcast_querier 0 \
					mcast_query_interval 12500 \
					mcast_startup_query_count 2 \
					mcast_mld_version 1
	ip link set br0 up type bridge vlan_filtering 0 \
					mcast_vlan_snooping 0 \
					mcast_stats_enabled 0
}

mldv2per_vlan_snooping_port_stp_test()
{
	mldv2per_vlan_snooping_stp_test 1
}

mldv2per_vlan_snooping_vlan_stp_test()
{
	mldv2per_vlan_snooping_stp_test 0
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="mldv2include_test mldv2inc_allow_test mldv2inc_is_include_test mldv2inc_is_exclude_test \
	   mldv2inc_to_exclude_test"
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
# MLDv2 allow report: grp ff02::cc allow 2001:db8:1::10,2001:db8:1::11,2001:db8:1::12
MZPKT_ALLOW="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:00:\
00:00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:\
02:00:00:00:00:8f:00:8a:ac:00:00:00:01:05:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:\
00:cc:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:10:20:01:0d:b8:00:01:00:00:00:00:00:00:00:\
00:00:11:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:12"
# MLDv2 is_ex report: grp ff02::cc is_exclude 2001:db8:1::1,2001:db8:1::2,2001:db8:1::20,2001:db8:1::21
MZPKT_IS_EXC="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:64:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:5f:d0:00:00:00:01:02:00:00:04:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:02:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:21"
# MLDv2 to_ex report: grp ff02::cc to_exclude 2001:db8:1::1,2001:db8:1::20,2001:db8:1::30
MZPKT_TO_EXC="33:33:00:00:00:01:fe:54:00:04:5e:ba:86:dd:60:0a:2d:ae:00:54:00:01:fe:80:00:00:00:\
00:00:00:fc:54:00:ff:fe:04:5e:ba:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:01:3a:00:05:02:00:\
00:00:00:8f:00:8b:8e:00:00:00:01:04:00:00:03:ff:02:00:00:00:00:00:00:00:00:00:00:00:00:00:cc:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01:20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:20:20:\
01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:30"

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

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

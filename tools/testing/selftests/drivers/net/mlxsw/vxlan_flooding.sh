#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test VxLAN flooding. The device stores flood records in a singly linked list
# where each record stores up to three IPv4 addresses of remote VTEPs. The test
# verifies that packets are correctly flooded in various cases such as deletion
# of a record in the middle of the list.
#
# +--------------------+
# | H1 (vrf)           |
# |    + $h1           |
# |    | 203.0.113.1/24|
# +----|---------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# | +--|--------------------------------------------------------------------+ |
# | |  + $swp1                   BR0 (802.1d)                               | |
# | |                                                                       | |
# | |  + vxlan0 (vxlan)                                                     | |
# | |    local 198.51.100.1                                                 | |
# | |    remote 198.51.100.{2..13}                                          | |
# | |    id 10 dstport 4789                                                 | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |  198.51.100.0/24 via 192.0.2.2                                            |
# |                                                                           |
# |    + $rp1                                                                 |
# |    | 192.0.2.1/24                                                         |
# +----|----------------------------------------------------------------------+
#      |
# +----|--------------------------------------------------------+
# |    |                                               R2 (vrf) |
# |    + $rp2                                                   |
# |      192.0.2.2/24                                           |
# |                                                             |
# +-------------------------------------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="flooding_test"
NUM_NETIFS=4
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

h1_create()
{
	simple_if_init $h1 203.0.113.1/24
}

h1_destroy()
{
	simple_if_fini $h1 203.0.113.1/24
}

switch_create()
{
	# Make sure the bridge uses the MAC address of the local port and
	# not that of the VxLAN's device
	ip link add dev br0 type bridge mcast_snooping 0
	ip link set dev br0 address $(mac_get $swp1)

	ip link add name vxlan0 type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	ip address add 198.51.100.1/32 dev lo

	ip link set dev $swp1 master br0
	ip link set dev vxlan0 master br0

	ip link set dev br0 up
	ip link set dev $swp1 up
	ip link set dev vxlan0 up
}

switch_destroy()
{
	ip link set dev vxlan0 down
	ip link set dev $swp1 down
	ip link set dev br0 down

	ip link set dev vxlan0 nomaster
	ip link set dev $swp1 nomaster

	ip address del 198.51.100.1/32 dev lo

	ip link del dev vxlan0

	ip link del dev br0
}

router1_create()
{
	# This router is in the default VRF, where the VxLAN device is
	# performing the L3 lookup
	ip link set dev $rp1 up
	ip address add 192.0.2.1/24 dev $rp1
	ip route add 198.51.100.0/24 via 192.0.2.2
}

router1_destroy()
{
	ip route del 198.51.100.0/24 via 192.0.2.2
	ip address del 192.0.2.1/24 dev $rp1
	ip link set dev $rp1 down
}

router2_create()
{
	# This router is not in the default VRF, so use simple_if_init()
	simple_if_init $rp2 192.0.2.2/24
}

router2_destroy()
{
	simple_if_fini $rp2 192.0.2.2/24
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	rp1=${NETIFS[p3]}
	rp2=${NETIFS[p4]}

	vrf_prepare

	h1_create

	switch_create

	router1_create
	router2_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	router2_destroy
	router1_destroy

	switch_destroy

	h1_destroy

	vrf_cleanup
}

flooding_remotes_add()
{
	local num_remotes=$1
	local lsb
	local i

	for i in $(eval echo {1..$num_remotes}); do
		lsb=$((i + 1))

		bridge fdb append 00:00:00:00:00:00 dev vxlan0 self \
			dst 198.51.100.$lsb
	done
}

flooding_filters_add()
{
	local num_remotes=$1
	local lsb
	local i

	tc qdisc add dev $rp2 clsact

	for i in $(eval echo {1..$num_remotes}); do
		lsb=$((i + 1))

		tc filter add dev $rp2 ingress protocol ip pref $i handle $i \
			flower ip_proto udp dst_ip 198.51.100.$lsb \
			dst_port 4789 skip_sw action drop
	done
}

flooding_filters_del()
{
	local num_remotes=$1
	local i

	for i in $(eval echo {1..$num_remotes}); do
		tc filter del dev $rp2 ingress protocol ip pref $i \
			handle $i flower
	done

	tc qdisc del dev $rp2 clsact
}

flooding_check_packets()
{
	local packets=("$@")
	local num_remotes=${#packets[@]}
	local i

	for i in $(eval echo {1..$num_remotes}); do
		tc_check_packets "dev $rp2 ingress" $i ${packets[i - 1]}
		check_err $? "remote $i - did not get expected number of packets"
	done
}

flooding_test()
{
	# Use 12 remote VTEPs that will be stored in 4 records. The array
	# 'packets' will store how many packets are expected to be received
	# by each remote VTEP at each stage of the test
	declare -a packets=(1 1 1 1 1 1 1 1 1 1 1 1)
	local num_remotes=12

	RET=0

	# Add FDB entries for remote VTEPs and corresponding tc filters on the
	# ingress of the nexthop router. These filters will count how many
	# packets were flooded to each remote VTEP
	flooding_remotes_add $num_remotes
	flooding_filters_add $num_remotes

	# Send one packet and make sure it is flooded to all the remote VTEPs
	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 1 packet"

	# Delete the third record which corresponds to VTEPs with LSB 8..10
	# and check that packet is flooded correctly when we remove a record
	# from the middle of the list
	RET=0

	packets=(2 2 2 2 2 2 1 1 1 2 2 2)
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.8
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.9
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.10

	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 2 packets"

	# Delete the first record and make sure the packet is flooded correctly
	RET=0

	packets=(2 2 2 3 3 3 1 1 1 3 3 3)
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.2
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.3
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.4

	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 3 packets"

	# Delete the last record and make sure the packet is flooded correctly
	RET=0

	packets=(2 2 2 4 4 4 1 1 1 3 3 3)
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.11
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.12
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.13

	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 4 packets"

	# Delete the last record, one entry at a time and make sure single
	# entries are correctly removed
	RET=0

	packets=(2 2 2 4 5 5 1 1 1 3 3 3)
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.5

	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 5 packets"

	RET=0

	packets=(2 2 2 4 5 6 1 1 1 3 3 3)
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.6

	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 6 packets"

	RET=0

	packets=(2 2 2 4 5 6 1 1 1 3 3 3)
	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.7

	$MZ $h1 -q -p 64 -b de:ad:be:ef:13:37 -t ip -c 1
	flooding_check_packets "${packets[@]}"
	log_test "flood after 7 packets"

	flooding_filters_del $num_remotes
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

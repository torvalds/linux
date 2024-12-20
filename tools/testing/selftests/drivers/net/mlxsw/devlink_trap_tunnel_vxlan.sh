#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap tunnel drops and exceptions functionality over mlxsw.
# Check all traps to make sure they are triggered under the right
# conditions.

# +--------------------+
# | H1 (vrf)           |
# |    + $h1           |
# |    | 192.0.2.1/28  |
# +----|---------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# | +--|--------------------------------------------------------------------+ |
# | |  + $swp1                   BR1 (802.1d)                               | |
# | |                                                                       | |
# | |  + vx1 (vxlan)                                                        | |
# | |    local 192.0.2.17                                                   | |
# | |    id 1000 dstport $VXPORT                                            | |
# | +-----------------------------------------------------------------------+ |
# |                                                                           |
# |    + $rp1                                                                 |
# |    | 192.0.2.17/28                                                        |
# +----|----------------------------------------------------------------------+
#      |
# +----|--------------------------------------------------------+
# |    |                                             VRF2       |
# |    + $rp2                                                   |
# |      192.0.2.18/28                                          |
# |                                                             |
# +-------------------------------------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	decap_error_test
	overlay_smac_is_mc_test
"

NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

: ${VXPORT:=4789}
export VXPORT

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

switch_create()
{
	ip link add name br1 type bridge vlan_filtering 0 mcast_snooping 0
	# Make sure the bridge uses the MAC address of the local port and not
	# that of the VxLAN's device.
	ip link set dev br1 address $(mac_get $swp1)
	ip link set dev br1 up

	tc qdisc add dev $swp1 clsact
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up

	ip link add name vx1 type vxlan id 1000 local 192.0.2.17 \
		dstport "$VXPORT" nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx1 master br1
	ip link set dev vx1 up

	ip address add dev $rp1 192.0.2.17/28
	ip link set dev $rp1 up
}

switch_destroy()
{
	ip link set dev $rp1 down
	ip address del dev $rp1 192.0.2.17/28

	ip link set dev vx1 down
	ip link set dev vx1 nomaster
	ip link del dev vx1

	ip link set dev $swp1 down
	ip link set dev $swp1 nomaster
	tc qdisc del dev $swp1 clsact

	ip link set dev br1 down
	ip link del dev br1
}

vrf2_create()
{
	simple_if_init $rp2 192.0.2.18/28
}

vrf2_destroy()
{
	simple_if_fini $rp2 192.0.2.18/28
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	rp1=${NETIFS[p3]}
	rp2=${NETIFS[p4]}

	vrf_prepare
	forwarding_enable
	h1_create
	switch_create
	vrf2_create
}

cleanup()
{
	pre_cleanup

	vrf2_destroy
	switch_destroy
	h1_destroy
	forwarding_restore
	vrf_cleanup
}

ecn_payload_get()
{
	dest_mac=$(mac_get $h1)
	p=$(:
		)"08:"$(                      : VXLAN flags
		)"00:00:00:"$(                : VXLAN reserved
		)"00:03:e8:"$(                : VXLAN VNI : 1000
		)"00:"$(                      : VXLAN reserved
		)"$dest_mac:"$(               : ETH daddr
		)"00:00:00:00:00:00:"$(       : ETH saddr
		)"08:00:"$(                   : ETH type
		)"45:"$(                      : IP version + IHL
		)"00:"$(                      : IP TOS
		)"00:14:"$(                   : IP total length
		)"00:00:"$(                   : IP identification
		)"20:00:"$(                   : IP flags + frag off
		)"40:"$(                      : IP TTL
		)"00:"$(                      : IP proto
		)"D6:E5:"$(                   : IP header csum
		)"c0:00:02:03:"$(             : IP saddr: 192.0.2.3
		)"c0:00:02:01:"$(             : IP daddr: 192.0.2.1
		)
	echo $p
}

ecn_decap_test()
{
	local trap_name="decap_error"
	local desc=$1; shift
	local ecn_desc=$1; shift
	local outer_tos=$1; shift
	local mz_pid

	RET=0

	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower src_ip 192.0.2.3 dst_ip 192.0.2.1 action pass

	rp1_mac=$(mac_get $rp1)
	payload=$(ecn_payload_get)

	ip vrf exec v$rp2 $MZ $rp2 -c 0 -d 1msec -b $rp1_mac -B 192.0.2.17 \
		-t udp sp=12345,dp=$VXPORT,tos=$outer_tos,p=$payload -q &
	mz_pid=$!

	devlink_trap_exception_test $trap_name

	tc_check_packets "dev $swp1 egress" 101 0
	check_err $? "Packets were not dropped"

	log_test "$desc: Inner ECN is not ECT and outer is $ecn_desc"

	kill_process $mz_pid
	tc filter del dev $swp1 egress protocol ip pref 1 handle 101 flower
}

reserved_bits_payload_get()
{
	dest_mac=$(mac_get $h1)
	p=$(:
		)"08:"$(                      : VXLAN flags
		)"01:00:00:"$(                : VXLAN reserved
		)"00:03:e8:"$(                : VXLAN VNI : 1000
		)"00:"$(                      : VXLAN reserved
		)"$dest_mac:"$(               : ETH daddr
		)"00:00:00:00:00:00:"$(       : ETH saddr
		)"08:00:"$(                   : ETH type
		)"45:"$(                      : IP version + IHL
		)"00:"$(                      : IP TOS
		)"00:14:"$(                   : IP total length
		)"00:00:"$(                   : IP identification
		)"20:00:"$(                   : IP flags + frag off
		)"40:"$(                      : IP TTL
		)"00:"$(                      : IP proto
		)"00:00:"$(                   : IP header csum
		)"c0:00:02:03:"$(             : IP saddr: 192.0.2.3
		)"c0:00:02:01:"$(             : IP daddr: 192.0.2.1
		)
	echo $p
}

short_payload_get()
{
        dest_mac=$(mac_get $h1)
        p=$(:
		)"08:"$(                      : VXLAN flags
		)"00:00:00:"$(                : VXLAN reserved
		)"00:03:e8:"$(                : VXLAN VNI : 1000
		)"00:"$(                      : VXLAN reserved
		)"$dest_mac:"$(               : ETH daddr
		)"00:00:00:00:00:00:"$(       : ETH saddr
		)
        echo $p
}

corrupted_packet_test()
{
	local trap_name="decap_error"
	local desc=$1; shift
	local payload_get=$1; shift
	local mz_pid

	RET=0

	# In case of too short packet, there is no any inner packet,
	# so the matching will always succeed
	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower skip_hw src_ip 192.0.2.3 dst_ip 192.0.2.1 action pass

	rp1_mac=$(mac_get $rp1)
	payload=$($payload_get)
	ip vrf exec v$rp2 $MZ $rp2 -c 0 -d 1msec -b $rp1_mac \
		-B 192.0.2.17 -t udp sp=12345,dp=$VXPORT,p=$payload -q &
	mz_pid=$!

	devlink_trap_exception_test $trap_name

	tc_check_packets "dev $swp1 egress" 101 0
	check_err $? "Packets were not dropped"

	log_test "$desc"

	kill_process $mz_pid
	tc filter del dev $swp1 egress protocol ip pref 1 handle 101 flower
}

decap_error_test()
{
	ecn_decap_test "Decap error" "ECT(1)" 01
	ecn_decap_test "Decap error" "ECT(0)" 02
	ecn_decap_test "Decap error" "CE" 03

	corrupted_packet_test "Decap error: Reserved bits in use" \
		"reserved_bits_payload_get"
	corrupted_packet_test "Decap error: Too short inner packet" \
		"short_payload_get"
}

mc_smac_payload_get()
{
	dest_mac=$(mac_get $h1)
	source_mac=01:02:03:04:05:06
	p=$(:
		)"08:"$(                      : VXLAN flags
		)"00:00:00:"$(                : VXLAN reserved
		)"00:03:e8:"$(                : VXLAN VNI : 1000
		)"00:"$(                      : VXLAN reserved
		)"$dest_mac:"$(               : ETH daddr
		)"$source_mac:"$(             : ETH saddr
		)"08:00:"$(                   : ETH type
		)"45:"$(                      : IP version + IHL
		)"00:"$(                      : IP TOS
		)"00:14:"$(                   : IP total length
		)"00:00:"$(                   : IP identification
		)"20:00:"$(                   : IP flags + frag off
		)"40:"$(                      : IP TTL
		)"00:"$(                      : IP proto
		)"00:00:"$(                   : IP header csum
		)"c0:00:02:03:"$(             : IP saddr: 192.0.2.3
		)"c0:00:02:01:"$(             : IP daddr: 192.0.2.1
		)
	echo $p
}

overlay_smac_is_mc_test()
{
	local trap_name="overlay_smac_is_mc"
	local mz_pid

	RET=0

	# The matching will be checked on devlink_trap_drop_test()
	# and the filter will be removed on devlink_trap_drop_cleanup()
	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower src_mac 01:02:03:04:05:06 action pass

	rp1_mac=$(mac_get $rp1)
	payload=$(mc_smac_payload_get)

	ip vrf exec v$rp2 $MZ $rp2 -c 0 -d 1msec -b $rp1_mac \
		-B 192.0.2.17 -t udp sp=12345,dp=$VXPORT,p=$payload -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $swp1 101

	log_test "Overlay source MAC is multicast"

	devlink_trap_drop_cleanup $mz_pid $swp1 "ip" 1 101
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS

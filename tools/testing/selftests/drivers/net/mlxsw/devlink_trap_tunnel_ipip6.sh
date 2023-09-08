#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap tunnel exceptions functionality over mlxsw.
# Check all exception traps to make sure they are triggered under the right
# conditions.

# +-------------------------+
# | H1                      |
# |               $h1 +     |
# |  2001:db8:1::1/64 |     |
# +-------------------|-----+
#                     |
# +-------------------|-----+
# | SW1               |     |
# |             $swp1 +     |
# |  2001:db8:1::2/64       |
# |                         |
# |  + g1 (ip6gre)          |
# |    loc=2001:db8:3::1    |
# |    rem=2001:db8:3::2    |
# |    tos=inherit          |
# |                         |
# |  + $rp1                 |
# |  | 2001:db8:10::1/64    |
# +--|----------------------+
#    |
# +--|----------------------+
# |  |                 VRF2 |
# |  + $rp2                 |
# |    2001:db8:10::2/64    |
# +-------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	decap_error_test
"

NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1 2001:db8:1::1/64
}

h1_destroy()
{
	simple_if_fini $h1 2001:db8:1::1/64
}

vrf2_create()
{
	simple_if_init $rp2 2001:db8:10::2/64
}

vrf2_destroy()
{
	simple_if_fini $rp2 2001:db8:10::2/64
}

switch_create()
{
	ip link set dev $swp1 up
	__addr_add_del $swp1 add 2001:db8:1::2/64
	tc qdisc add dev $swp1 clsact

	tunnel_create g1 ip6gre 2001:db8:3::1 2001:db8:3::2 tos inherit \
		ttl inherit
	ip link set dev g1 up
	__addr_add_del g1 add 2001:db8:3::1/128

	ip link set dev $rp1 up
	__addr_add_del $rp1 add 2001:db8:10::1/64
}

switch_destroy()
{
	__addr_add_del $rp1 del 2001:db8:10::1/64
	ip link set dev $rp1 down

	__addr_add_del g1 del 2001:db8:3::1/128
	ip link set dev g1 down
	tunnel_destroy g1

	tc qdisc del dev $swp1 clsact
	__addr_add_del $swp1 del 2001:db8:1::2/64
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	rp1=${NETIFS[p3]}
	rp2=${NETIFS[p4]}

	forwarding_enable
	vrf_prepare
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
	vrf_cleanup
	forwarding_restore
}

ipip_payload_get()
{
	local saddr="20:01:0d:b8:00:02:00:00:00:00:00:00:00:00:00:01"
	local daddr="20:01:0d:b8:00:01:00:00:00:00:00:00:00:00:00:01"
	local flags=$1; shift
	local key=$1; shift

	p=$(:
		)"$flags"$(		      : GRE flags
	        )"0:00:"$(                    : Reserved + version
		)"86:dd:"$(		      : ETH protocol type
		)"$key"$( 		      : Key
		)"6"$(	                      : IP version
		)"0:0"$(		      : Traffic class
		)"0:00:00:"$(		      : Flow label
		)"00:00:"$(                   : Payload length
		)"3a:"$(                      : Next header
		)"04:"$(                      : Hop limit
		)"$saddr:"$(                  : IP saddr
		)"$daddr:"$(                  : IP daddr
		)
	echo $p
}

ecn_payload_get()
{
	echo $(ipip_payload_get "0")
}

ecn_decap_test()
{
	local trap_name="decap_error"
	local desc=$1; shift
	local ecn_desc=$1; shift
	local outer_tos=$1; shift
	local mz_pid

	RET=0

	tc filter add dev $swp1 egress protocol ipv6 pref 1 handle 101 \
		flower src_ip 2001:db8:2::1 dst_ip 2001:db8:1::1 skip_sw \
		action pass

	rp1_mac=$(mac_get $rp1)
	rp2_mac=$(mac_get $rp2)
	payload=$(ecn_payload_get)

	ip vrf exec v$rp2 $MZ -6 $rp2 -c 0 -d 1msec -a $rp2_mac -b $rp1_mac \
		-A 2001:db8:3::2 -B 2001:db8:3::1 -t ip \
			tos=$outer_tos,next=47,p=$payload -q &
	mz_pid=$!

	devlink_trap_exception_test $trap_name

	tc_check_packets "dev $swp1 egress" 101 0
	check_err $? "Packets were not dropped"

	log_test "$desc: Inner ECN is not ECT and outer is $ecn_desc"

	kill $mz_pid && wait $mz_pid &> /dev/null
	tc filter del dev $swp1 egress protocol ipv6 pref 1 handle 101 flower
}

no_matching_tunnel_test()
{
	local trap_name="decap_error"
	local desc=$1; shift
	local sip=$1; shift
	local mz_pid

	RET=0

	tc filter add dev $swp1 egress protocol ipv6 pref 1 handle 101 \
		flower src_ip 2001:db8:2::1 dst_ip 2001:db8:1::1 action pass

	rp1_mac=$(mac_get $rp1)
	rp2_mac=$(mac_get $rp2)
	payload=$(ipip_payload_get "$@")

	ip vrf exec v$rp2 $MZ -6 $rp2 -c 0 -d 1msec -a $rp2_mac -b $rp1_mac \
		-A $sip -B 2001:db8:3::1 -t ip next=47,p=$payload -q &
	mz_pid=$!

	devlink_trap_exception_test $trap_name

	tc_check_packets "dev $swp1 egress" 101 0
	check_err $? "Packets were not dropped"

	log_test "$desc"

	kill $mz_pid && wait $mz_pid &> /dev/null
	tc filter del dev $swp1 egress protocol ipv6 pref 1 handle 101 flower
}

decap_error_test()
{
	# Correct source IP - the remote address
	local sip=2001:db8:3::2

	ecn_decap_test "Decap error" "ECT(1)" 01
	ecn_decap_test "Decap error" "ECT(0)" 02
	ecn_decap_test "Decap error" "CE" 03

	no_matching_tunnel_test "Decap error: Source IP check failed" \
		2001:db8:4::2 "0"
	no_matching_tunnel_test \
		"Decap error: Key exists but was not expected" $sip "2" \
		"00:00:00:E9:"

	# Destroy the tunnel and create new one with key
	__addr_add_del g1 del 2001:db8:3::1/128
	tunnel_destroy g1

	tunnel_create g1 ip6gre 2001:db8:3::1 2001:db8:3::2 tos inherit \
		ttl inherit key 233
	__addr_add_del g1 add 2001:db8:3::1/128

	no_matching_tunnel_test \
		"Decap error: Key does not exist but was expected" $sip "0"
	no_matching_tunnel_test \
		"Decap error: Packet has a wrong key field" $sip "2" \
		"00:00:00:E8:"
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS

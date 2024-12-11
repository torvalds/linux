#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap tunnel exceptions functionality over mlxsw.
# Check all exception traps to make sure they are triggered under the right
# conditions.

# +-------------------------+
# | H1                      |
# |               $h1 +     |
# |      192.0.2.1/28 |     |
# +-------------------|-----+
#                     |
# +-------------------|-----+
# | SW1               |     |
# |             $swp1 +     |
# |      192.0.2.2/28       |
# |                         |
# |  + g1a (gre)            |
# |    loc=192.0.2.65       |
# |    rem=192.0.2.66       |
# |    tos=inherit          |
# |                         |
# |  + $rp1                 |
# |  |  198.51.100.1/28     |
# +--|----------------------+
#    |
# +--|----------------------+
# |  |                 VRF2 |
# |  + $rp2                 |
# |    198.51.100.2/28      |
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
	simple_if_init $h1 192.0.2.1/28
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

vrf2_create()
{
	simple_if_init $rp2 198.51.100.2/28
}

vrf2_destroy()
{
	simple_if_fini $rp2 198.51.100.2/28
}

switch_create()
{
	__addr_add_del $swp1 add 192.0.2.2/28
	tc qdisc add dev $swp1 clsact
	ip link set dev $swp1 up

	tunnel_create g1 gre 192.0.2.65 192.0.2.66 tos inherit
	__addr_add_del g1 add 192.0.2.65/32
	ip link set dev g1 up

	__addr_add_del $rp1 add 198.51.100.1/28
	ip link set dev $rp1 up
}

switch_destroy()
{
	ip link set dev $rp1 down
	__addr_add_del $rp1 del 198.51.100.1/28

	ip link set dev g1 down
	__addr_add_del g1 del 192.0.2.65/32
	tunnel_destroy g1

	ip link set dev $swp1 down
	tc qdisc del dev $swp1 clsact
	__addr_add_del $swp1 del 192.0.2.2/28
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
	local flags=$1; shift
	local key=$1; shift

	p=$(:
		)"$flags"$(		      : GRE flags
	        )"0:00:"$(                    : Reserved + version
		)"08:00:"$(		      : ETH protocol type
		)"$key"$( 		      : Key
		)"4"$(	                      : IP version
		)"5:"$(                       : IHL
		)"00:"$(                      : IP TOS
		)"00:14:"$(                   : IP total length
		)"00:00:"$(                   : IP identification
		)"20:00:"$(                   : IP flags + frag off
		)"30:"$(                      : IP TTL
		)"01:"$(                      : IP proto
		)"E7:E6:"$(    	              : IP header csum
		)"C0:00:01:01:"$(             : IP saddr : 192.0.1.1
		)"C0:00:02:01:"$(             : IP daddr : 192.0.2.1
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

	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower src_ip 192.0.1.1 dst_ip 192.0.2.1 action pass

	rp1_mac=$(mac_get $rp1)
	rp2_mac=$(mac_get $rp2)
	payload=$(ecn_payload_get)

	ip vrf exec v$rp2 $MZ $rp2 -c 0 -d 1msec -a $rp2_mac -b $rp1_mac \
		-A 192.0.2.66 -B 192.0.2.65 -t ip \
			len=48,tos=$outer_tos,proto=47,p=$payload -q &

	mz_pid=$!

	devlink_trap_exception_test $trap_name

	tc_check_packets "dev $swp1 egress" 101 0
	check_err $? "Packets were not dropped"

	log_test "$desc: Inner ECN is not ECT and outer is $ecn_desc"

	kill_process $mz_pid
	tc filter del dev $swp1 egress protocol ip pref 1 handle 101 flower
}

no_matching_tunnel_test()
{
	local trap_name="decap_error"
	local desc=$1; shift
	local sip=$1; shift
	local mz_pid

	RET=0

	tc filter add dev $swp1 egress protocol ip pref 1 handle 101 \
		flower src_ip 192.0.1.1 dst_ip 192.0.2.1 action pass

	rp1_mac=$(mac_get $rp1)
	rp2_mac=$(mac_get $rp2)
	payload=$(ipip_payload_get "$@")

	ip vrf exec v$rp2 $MZ $rp2 -c 0 -d 1msec -a $rp2_mac -b $rp1_mac \
		-A $sip -B 192.0.2.65 -t ip len=48,proto=47,p=$payload -q &
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
	# Correct source IP - the remote address
	local sip=192.0.2.66

	ecn_decap_test "Decap error" "ECT(1)" 01
	ecn_decap_test "Decap error" "ECT(0)" 02
	ecn_decap_test "Decap error" "CE" 03

	no_matching_tunnel_test "Decap error: Source IP check failed" \
		192.0.2.68 "0"
	no_matching_tunnel_test \
		"Decap error: Key exists but was not expected" $sip "2" \
		"00:00:00:E9:"

	# Destroy the tunnel and create new one with key
	__addr_add_del g1 del 192.0.2.65/32
	tunnel_destroy g1

	tunnel_create g1 gre 192.0.2.65 192.0.2.66 tos inherit key 233
	__addr_add_del g1 add 192.0.2.65/32

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

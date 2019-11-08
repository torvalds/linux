#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test devlink-trap L3 drops functionality over mlxsw. Each registered L3 drop
# packet trap is tested to make sure it is triggered under the right
# conditions.

# +---------------------------------+
# | H1 (vrf)                        |
# |    + $h1                        |
# |    | 192.0.2.1/24               |
# |    | 2001:db8:1::1/64           |
# |    |                            |
# |    |  default via 192.0.2.2     |
# |    |  default via 2001:db8:1::2 |
# +----|----------------------------+
#      |
# +----|----------------------------------------------------------------------+
# | SW |                                                                      |
# |    + $rp1                                                                 |
# |        192.0.2.2/24                                                       |
# |        2001:db8:1::2/64                                                   |
# |                                                                           |
# |        2001:db8:2::2/64                                                   |
# |        198.51.100.2/24                                                    |
# |    + $rp2                                                                 |
# |    |                                                                      |
# +----|----------------------------------------------------------------------+
#      |
# +----|----------------------------+
# |    |  default via 198.51.100.2  |
# |    |  default via 2001:db8:2::2 |
# |    |                            |
# |    | 2001:db8:2::1/64           |
# |    | 198.51.100.1/24            |
# |    + $h2                        |
# | H2 (vrf)                        |
# +---------------------------------+

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	non_ip_test
	uc_dip_over_mc_dmac_test
	dip_is_loopback_test
	sip_is_mc_test
	sip_is_loopback_test
	ip_header_corrupted_test
	ipv4_sip_is_limited_bc_test
	ipv6_mc_dip_reserved_scope_test
	ipv6_mc_dip_interface_local_scope_test
	blackhole_route_test
"

NUM_NETIFS=4
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 2001:db8:1::1/64

	ip -4 route add default vrf v$h1 nexthop via 192.0.2.2
	ip -6 route add default vrf v$h1 nexthop via 2001:db8:1::2
}

h1_destroy()
{
	ip -6 route del default vrf v$h1 nexthop via 2001:db8:1::2
	ip -4 route del default vrf v$h1 nexthop via 192.0.2.2

	simple_if_fini $h1 192.0.2.1/24 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 $h2_ipv4/24 $h2_ipv6/64

	ip -4 route add default vrf v$h2 nexthop via 198.51.100.2
	ip -6 route add default vrf v$h2 nexthop via 2001:db8:2::2
}

h2_destroy()
{
	ip -6 route del default vrf v$h2 nexthop via 2001:db8:2::2
	ip -4 route del default vrf v$h2 nexthop via 198.51.100.2

	simple_if_fini $h2 $h2_ipv4/24 $h2_ipv6/64
}

router_create()
{
	ip link set dev $rp1 up
	ip link set dev $rp2 up

	tc qdisc add dev $rp2 clsact

	__addr_add_del $rp1 add 192.0.2.2/24 2001:db8:1::2/64
	__addr_add_del $rp2 add 198.51.100.2/24 2001:db8:2::2/64
}

router_destroy()
{
	__addr_add_del $rp2 del 198.51.100.2/24 2001:db8:2::2/64
	__addr_add_del $rp1 del 192.0.2.2/24 2001:db8:1::2/64

	tc qdisc del dev $rp2 clsact
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rp1=${NETIFS[p2]}

	rp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	h1mac=$(mac_get $h1)
	rp1mac=$(mac_get $rp1)

	h1_ipv4=192.0.2.1
	h2_ipv4=198.51.100.1
	h1_ipv6=2001:db8:1::1
	h2_ipv6=2001:db8:2::1

	vrf_prepare
	forwarding_enable

	h1_create
	h2_create

	router_create
}

cleanup()
{
	pre_cleanup

	router_destroy

	h2_destroy
	h1_destroy

	forwarding_restore
	vrf_cleanup
}

ping_check()
{
	trap_name=$1; shift

	devlink_trap_action_set $trap_name "trap"
	ping_do $h1 $h2_ipv4
	check_err $? "Packets that should not be trapped were trapped"
	devlink_trap_action_set $trap_name "drop"
}

non_ip_test()
{
	local trap_name="non_ip"
	local group_name="l3_drops"
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 \
		flower dst_ip $h2_ipv4 action drop

	# Generate non-IP packets to the router
	$MZ $h1 -c 0 -p 100 -d 1msec -B $h2_ipv4 -q "$rp1mac $h1mac \
		00:00 de:ad:be:ef" &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "Non IP"

	devlink_trap_drop_cleanup $mz_pid $rp2 "ip"
}

__uc_dip_over_mc_dmac_test()
{
	local desc=$1; shift
	local proto=$1; shift
	local dip=$1; shift
	local flags=${1:-""}; shift
	local trap_name="uc_dip_over_mc_dmac"
	local group_name="l3_drops"
	local dmac=01:02:03:04:05:06
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol $proto pref 1 handle 101 \
		flower ip_proto udp src_port 54321 dst_port 12345 action drop

	# Generate IP packets with a unicast IP and a multicast destination MAC
	$MZ $h1 $flags -t udp "sp=54321,dp=12345" -c 0 -p 100 -b $dmac \
		-B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "Unicast destination IP over multicast destination MAC: $desc"

	devlink_trap_drop_cleanup $mz_pid $rp2 $proto
}

uc_dip_over_mc_dmac_test()
{
	__uc_dip_over_mc_dmac_test "IPv4" "ip" $h2_ipv4
	__uc_dip_over_mc_dmac_test "IPv6" "ipv6" $h2_ipv6 "-6"
}

__sip_is_loopback_test()
{
	local desc=$1; shift
	local proto=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local flags=${1:-""}; shift
	local trap_name="sip_is_loopback_address"
	local group_name="l3_drops"
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol $proto pref 1 handle 101 \
		flower src_ip $sip action drop

	# Generate packets with loopback source IP
	$MZ $h1 $flags -t udp "sp=54321,dp=12345" -c 0 -p 100 -A $sip \
		-b $rp1mac -B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "Source IP is loopback address: $desc"

	devlink_trap_drop_cleanup $mz_pid $rp2 $proto
}

sip_is_loopback_test()
{
	__sip_is_loopback_test "IPv4" "ip" "127.0.0.0/8" $h2_ipv4
	__sip_is_loopback_test "IPv6" "ipv6" "::1" $h2_ipv6 "-6"
}

__dip_is_loopback_test()
{
	local desc=$1; shift
	local proto=$1; shift
	local dip=$1; shift
	local flags=${1:-""}; shift
	local trap_name="dip_is_loopback_address"
	local group_name="l3_drops"
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol $proto pref 1 handle 101 \
		flower dst_ip $dip action drop

	# Generate packets with loopback destination IP
	$MZ $h1 $flags -t udp "sp=54321,dp=12345" -c 0 -p 100 -b $rp1mac \
		-B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "Destination IP is loopback address: $desc"

	devlink_trap_drop_cleanup $mz_pid $rp2 $proto
}

dip_is_loopback_test()
{
	__dip_is_loopback_test "IPv4" "ip" "127.0.0.0/8"
	__dip_is_loopback_test "IPv6" "ipv6" "::1" "-6"
}

__sip_is_mc_test()
{
	local desc=$1; shift
	local proto=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local flags=${1:-""}; shift
	local trap_name="sip_is_mc"
	local group_name="l3_drops"
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol $proto pref 1 handle 101 \
		flower src_ip $sip action drop

	# Generate packets with multicast source IP
	$MZ $h1 $flags -t udp "sp=54321,dp=12345" -c 0 -p 100 -A $sip \
		-b $rp1mac -B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "Source IP is multicast: $desc"

	devlink_trap_drop_cleanup $mz_pid $rp2 $proto
}

sip_is_mc_test()
{
	__sip_is_mc_test "IPv4" "ip" "239.1.1.1" $h2_ipv4
	__sip_is_mc_test "IPv6" "ipv6" "FF02::2" $h2_ipv6 "-6"
}

ipv4_sip_is_limited_bc_test()
{
	local trap_name="ipv4_sip_is_limited_bc"
	local group_name="l3_drops"
	local sip=255.255.255.255
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 \
		flower src_ip $sip action drop

	# Generate packets with limited broadcast source IP
	$MZ $h1 -t udp "sp=54321,dp=12345" -c 0 -p 100 -A $sip -b $rp1mac \
		-B $h2_ipv4 -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "IPv4 source IP is limited broadcast"

	devlink_trap_drop_cleanup $mz_pid $rp2 "ip"
}

ipv4_payload_get()
{
	local ipver=$1; shift
	local ihl=$1; shift
	local checksum=$1; shift

	p=$(:
		)"08:00:"$(                   : ETH type
		)"$ipver"$(                   : IP version
		)"$ihl:"$(                    : IHL
		)"00:"$(		      : IP TOS
		)"00:F4:"$(                   : IP total length
		)"00:00:"$(                   : IP identification
		)"20:00:"$(                   : IP flags + frag off
		)"30:"$(                      : IP TTL
		)"01:"$(                      : IP proto
		)"$checksum:"$(               : IP header csum
		)"$h1_ipv4:"$(                : IP saddr
	        )"$h2_ipv4:"$(                : IP daddr
		)
	echo $p
}

__ipv4_header_corrupted_test()
{
	local desc=$1; shift
	local ipver=$1; shift
	local ihl=$1; shift
	local checksum=$1; shift
	local trap_name="ip_header_corrupted"
	local group_name="l3_drops"
	local payload
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 \
		flower dst_ip $h2_ipv4 action drop

	payload=$(ipv4_payload_get $ipver $ihl $checksum)

	# Generate packets with corrupted IP header
	$MZ $h1 -c 0 -d 1msec -a $h1mac -b $rp1mac -q p=$payload &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "IP header corrupted: $desc: IPv4"

	devlink_trap_drop_cleanup $mz_pid $rp2 "ip"
}

ipv6_payload_get()
{
	local ipver=$1; shift

	p=$(:
		)"86:DD:"$(                  : ETH type
		)"$ipver"$(                  : IP version
		)"0:0:"$(                    : Traffic class
		)"0:00:00:"$(		     : Flow label
		)"00:00:"$(                  : Payload length
		)"01:"$(                     : Next header
		)"04:"$(                     : Hop limit
		)"$h1_ipv6:"$(      	     : IP saddr
		)"$h2_ipv6:"$(               : IP daddr
		)
	echo $p
}

__ipv6_header_corrupted_test()
{
	local desc=$1; shift
	local ipver=$1; shift
	local trap_name="ip_header_corrupted"
	local group_name="l3_drops"
	local payload
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol ip pref 1 handle 101 \
		flower dst_ip $h2_ipv4 action drop

	payload=$(ipv6_payload_get $ipver)

	# Generate packets with corrupted IP header
	$MZ $h1 -c 0 -d 1msec -a $h1mac -b $rp1mac -q p=$payload &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "IP header corrupted: $desc: IPv6"

	devlink_trap_drop_cleanup $mz_pid $rp2 "ip"
}

ip_header_corrupted_test()
{
	# Each test uses one wrong value. The three values below are correct.
	local ipv="4"
	local ihl="5"
	local checksum="00:F4"

	__ipv4_header_corrupted_test "wrong IP version" 5 $ihl $checksum
	__ipv4_header_corrupted_test "wrong IHL" $ipv 4 $checksum
	__ipv4_header_corrupted_test "wrong checksum" $ipv $ihl "00:00"
	__ipv6_header_corrupted_test "wrong IP version" 5
}

ipv6_mc_dip_reserved_scope_test()
{
	local trap_name="ipv6_mc_dip_reserved_scope"
	local group_name="l3_drops"
	local dip=FF00::
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol ipv6 pref 1 handle 101 \
		flower dst_ip $dip action drop

	# Generate packets with reserved scope destination IP
	$MZ $h1 -6 -t udp "sp=54321,dp=12345" -c 0 -p 100 -b \
		"33:33:00:00:00:00" -B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "IPv6 multicast destination IP reserved scope"

	devlink_trap_drop_cleanup $mz_pid $rp2 "ipv6"
}

ipv6_mc_dip_interface_local_scope_test()
{
	local trap_name="ipv6_mc_dip_interface_local_scope"
	local group_name="l3_drops"
	local dip=FF01::
	local mz_pid

	RET=0

	ping_check $trap_name

	tc filter add dev $rp2 egress protocol ipv6 pref 1 handle 101 \
		flower dst_ip $dip action drop

	# Generate packets with interface local scope destination IP
	$MZ $h1 -6 -t udp "sp=54321,dp=12345" -c 0 -p 100 -b \
		"33:33:00:00:00:00" -B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2

	log_test "IPv6 multicast destination IP interface-local scope"

	devlink_trap_drop_cleanup $mz_pid $rp2 "ipv6"
}

__blackhole_route_test()
{
	local flags=$1; shift
	local subnet=$1; shift
	local proto=$1; shift
	local dip=$1; shift
	local ip_proto=${1:-"icmp"}; shift
	local trap_name="blackhole_route"
	local group_name="l3_drops"
	local mz_pid

	RET=0

	ping_check $trap_name

	ip -$flags route add blackhole $subnet
	tc filter add dev $rp2 egress protocol $proto pref 1 handle 101 \
		flower skip_hw dst_ip $dip ip_proto $ip_proto action drop

	# Generate packets to the blackhole route
	$MZ $h1 -$flags -t udp "sp=54321,dp=12345" -c 0 -p 100 -b $rp1mac \
		-B $dip -d 1msec -q &
	mz_pid=$!

	devlink_trap_drop_test $trap_name $group_name $rp2
	log_test "Blackhole route: IPv$flags"

	devlink_trap_drop_cleanup $mz_pid $rp2 $proto
	ip -$flags route del blackhole $subnet
}

blackhole_route_test()
{
	__blackhole_route_test "4" "198.51.100.0/30" "ip" $h2_ipv4
	__blackhole_route_test "6" "2001:db8:2::/120" "ipv6" $h2_ipv6 "icmpv6"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

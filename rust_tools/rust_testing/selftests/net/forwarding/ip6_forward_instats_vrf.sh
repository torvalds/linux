#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test ipv6 stats on the incoming if when forwarding with VRF

ALL_TESTS="
	ipv6_ping
	ipv6_in_too_big_err
	ipv6_in_hdr_err
	ipv6_in_addr_err
	ipv6_in_discard
"

NUM_NETIFS=4
source lib.sh

require_command $TROUTE6

h1_create()
{
	simple_if_init $h1 2001:1:1::2/64
	ip -6 route add vrf v$h1 2001:1:2::/64 via 2001:1:1::1
}

h1_destroy()
{
	ip -6 route del vrf v$h1 2001:1:2::/64 via 2001:1:1::1
	simple_if_fini $h1 2001:1:1::2/64
}

router_create()
{
	vrf_create router
	__simple_if_init $rtr1 router 2001:1:1::1/64
	__simple_if_init $rtr2 router 2001:1:2::1/64
	mtu_set $rtr2 1280
}

router_destroy()
{
	mtu_restore $rtr2
	__simple_if_fini $rtr2 2001:1:2::1/64
	__simple_if_fini $rtr1 2001:1:1::1/64
	vrf_destroy router
}

h2_create()
{
	simple_if_init $h2 2001:1:2::2/64
	ip -6 route add vrf v$h2 2001:1:1::/64 via 2001:1:2::1
	mtu_set $h2 1280
}

h2_destroy()
{
	mtu_restore $h2
	ip -6 route del vrf v$h2 2001:1:1::/64 via 2001:1:2::1
	simple_if_fini $h2 2001:1:2::2/64
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	rtr1=${NETIFS[p2]}

	rtr2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	vrf_prepare
	h1_create
	router_create
	h2_create

	forwarding_enable
}

cleanup()
{
	pre_cleanup

	forwarding_restore

	h2_destroy
	router_destroy
	h1_destroy
	vrf_cleanup
}

ipv6_in_too_big_err()
{
	RET=0

	local t0=$(ipv6_stats_get $rtr1 Ip6InTooBigErrors)
	local vrf_name=$(master_name_get $h1)

	# Send too big packets
	ip vrf exec $vrf_name \
		$PING6 -s 1300 2001:1:2::2 -c 1 -w $PING_TIMEOUT &> /dev/null

	local t1=$(ipv6_stats_get $rtr1 Ip6InTooBigErrors)
	test "$((t1 - t0))" -ne 0
	check_err $?
	log_test "Ip6InTooBigErrors"
}

ipv6_in_hdr_err()
{
	RET=0

	local t0=$(ipv6_stats_get $rtr1 Ip6InHdrErrors)
	local vrf_name=$(master_name_get $h1)

	# Send packets with hop limit 1, easiest with traceroute6 as some ping6
	# doesn't allow hop limit to be specified
	ip vrf exec $vrf_name \
		$TROUTE6 2001:1:2::2 &> /dev/null

	local t1=$(ipv6_stats_get $rtr1 Ip6InHdrErrors)
	test "$((t1 - t0))" -ne 0
	check_err $?
	log_test "Ip6InHdrErrors"
}

ipv6_in_addr_err()
{
	RET=0

	local t0=$(ipv6_stats_get $rtr1 Ip6InAddrErrors)
	local vrf_name=$(master_name_get $h1)

	# Disable forwarding temporary while sending the packet
	sysctl -qw net.ipv6.conf.all.forwarding=0
	ip vrf exec $vrf_name \
		$PING6 2001:1:2::2 -c 1 -w $PING_TIMEOUT &> /dev/null
	sysctl -qw net.ipv6.conf.all.forwarding=1

	local t1=$(ipv6_stats_get $rtr1 Ip6InAddrErrors)
	test "$((t1 - t0))" -ne 0
	check_err $?
	log_test "Ip6InAddrErrors"
}

ipv6_in_discard()
{
	RET=0

	local t0=$(ipv6_stats_get $rtr1 Ip6InDiscards)
	local vrf_name=$(master_name_get $h1)

	# Add a policy to discard
	ip xfrm policy add dst 2001:1:2::2/128 dir fwd action block
	ip vrf exec $vrf_name \
		$PING6 2001:1:2::2 -c 1 -w $PING_TIMEOUT &> /dev/null
	ip xfrm policy del dst 2001:1:2::2/128 dir fwd

	local t1=$(ipv6_stats_get $rtr1 Ip6InDiscards)
	test "$((t1 - t0))" -ne 0
	check_err $?
	log_test "Ip6InDiscards"
}
ipv6_ping()
{
	RET=0

	ping6_test $h1 2001:1:2::2
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS

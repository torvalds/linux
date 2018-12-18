#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that enough headroom is reserved for the first packet passing through an
# IPv6 GRE-like netdevice.

setup_prepare()
{
	ip link add h1 type veth peer name swp1
	ip link add h3 type veth peer name swp3

	ip link set dev h1 up
	ip address add 192.0.2.1/28 dev h1

	ip link add dev vh3 type vrf table 20
	ip link set dev h3 master vh3
	ip link set dev vh3 up
	ip link set dev h3 up

	ip link set dev swp3 up
	ip address add dev swp3 2001:db8:2::1/64
	ip address add dev swp3 2001:db8:2::3/64

	ip link set dev swp1 up
	tc qdisc add dev swp1 clsact

	ip link add name er6 type ip6erspan \
	   local 2001:db8:2::1 remote 2001:db8:2::2 oseq okey 123
	ip link set dev er6 up

	ip link add name gt6 type ip6gretap \
	   local 2001:db8:2::3 remote 2001:db8:2::4
	ip link set dev gt6 up

	sleep 1
}

cleanup()
{
	ip link del dev gt6
	ip link del dev er6
	ip link del dev swp1
	ip link del dev swp3
	ip link del dev vh3
}

test_headroom()
{
	local type=$1; shift
	local tundev=$1; shift

	tc filter add dev swp1 ingress pref 1000 matchall skip_hw \
		action mirred egress mirror dev $tundev
	ping -I h1 192.0.2.2 -c 1 -w 2 &> /dev/null
	tc filter del dev swp1 ingress pref 1000

	# If it doesn't panic, it passes.
	printf "TEST: %-60s  [PASS]\n" "$type headroom"
}

trap cleanup EXIT

setup_prepare
test_headroom ip6gretap gt6
test_headroom ip6erspan er6

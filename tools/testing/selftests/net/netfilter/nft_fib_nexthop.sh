#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2154
#
# Exercise nft_fib6_eval()'s sibling/nh enumeration on three route shapes:
#   1) route via a single external nexthop (nhid)
#   2) route via an external nexthop group (nhid -> group, two members)
#   3) route via old-style multipath (nexthop ... nexthop ...)
#
# In each scenario the route's nexthop set contains veth0 (the iif of the
# test packet). nft_fib6_info_nh_uses_dev() must walk the set and report
# veth0 as a valid oif. For (2) and (3) the matching nexthop is the second
# member, so the walk has to traverse beyond the primary nh.
#
# After sending $PKTS ICMPv6 echo requests from ns1, check two counters on
# nsrouter:
#   nf_ok  -- `fib daddr . iif oif eq "veth0"`  must equal $PKTS
#   nf_bad -- `fib daddr . iif oif missing`     must stay at 0
# Both rules also match on iif veth0 and ip6 daddr dead:dead::/64 so that
# kernel-generated ND/MLD/RA traffic cannot pollute the counters.
#
# Topology similar to nft_fib.sh, without ns2; two dummy interfaces on
# nsrouter host extra nh devices:
#
#   dead:1::99             dead:1::1
#       ns1 <----veth----> nsrouter --- dummy0 dead:2::1
#                                   \-- dummy1 dead:9::1

source lib.sh

ret=0
PKTS=3

checktool "nft --version" "run test without nft"
checktool "ip -V"         "run test without iproute2"

setup_ns nsrouter ns1
trap cleanup_all_ns EXIT

if ! ip link add veth0 netns "$nsrouter" type veth peer name eth0 netns "$ns1" \
	> /dev/null 2>&1; then
	echo "SKIP: No virtual ethernet pair device support in kernel"
	exit $ksft_skip
fi

ip -net "$ns1" link set lo up
ip -net "$ns1" link set eth0 up
ip -net "$ns1" -6 addr add dead:1::99/64 dev eth0 nodad
ip -net "$ns1" -6 route add default via dead:1::1

ip -net "$nsrouter" link set lo up
ip -net "$nsrouter" link set veth0 up
ip -net "$nsrouter" -6 addr add dead:1::1/64 dev veth0 nodad

if ! ip -net "$nsrouter" link add dummy0 type dummy 2>/dev/null; then
	echo "SKIP: dummy netdev not available"
	exit $ksft_skip
fi
ip -net "$nsrouter" link set dummy0 up
ip -net "$nsrouter" -6 addr add dead:2::1/64 dev dummy0 nodad

ip -net "$nsrouter" link add dummy1 type dummy
ip -net "$nsrouter" link set dummy1 up
ip -net "$nsrouter" -6 addr add dead:9::1/64 dev dummy1 nodad

ip netns exec "$nsrouter" sysctl -q net.ipv6.conf.all.forwarding=1

load_fib_rule() {
	# filter on iif + daddr so the counters only see our test packets
	ip netns exec "$nsrouter" nft -f /dev/stdin <<EOF
flush ruleset
table ip6 t {
	counter nf_ok  { }
	counter nf_bad { }
	chain c {
		type filter hook prerouting priority 0; policy accept;
		iif "veth0" ip6 daddr dead:dead::/64 fib daddr . iif oif eq "veth0" counter name nf_ok
		iif "veth0" ip6 daddr dead:dead::/64 fib daddr . iif oif missing    counter name nf_bad
	}
}
EOF
}

bad_counter() {
	local counter=$1
	local expect=$2
	local tag=$3

	echo "FAIL ($tag): counter $counter has unexpected value (expected \"$expect\")" 1>&2
	ip netns exec "$nsrouter" nft list counter ip6 t "$counter" 1>&2
}

run_scenario() {
	local what="$1"; shift
	# counter output format is "packets PACKET_NUM bytes BYTES_NUM";
	# we only care about the packet count
	local expect_ok="packets $PKTS bytes"
	local expect_bad="packets 0 bytes"
	local lret=0

	# reset route + nexthop state between scenarios
	ip -net "$nsrouter" -6 route del dead:dead::/64 > /dev/null 2>&1 || true
	ip -net "$nsrouter" nexthop flush               > /dev/null 2>&1 || true

	# run the scenario function passed by the caller
	"$@" || echo "WARN ($what): scenario setup returned non-zero"

	load_fib_rule || { echo "FAIL ($what): nft load"; ret=1; return; }

	# ping a daddr inside dead:dead::/64 so fib has to walk the nh set
	ip netns exec "$ns1" ping -6 -c "$PKTS" -i 0.1 -W 1 dead:dead::1 \
		> /dev/null 2>&1 || true

	# verify the packets went through the expected fib path
	if ! ip netns exec "$nsrouter" nft list counter ip6 t nf_ok | grep -q "$expect_ok"; then
		bad_counter nf_ok "$expect_ok" "$what"
		lret=1
	fi
	if ! ip netns exec "$nsrouter" nft list counter ip6 t nf_bad | grep -q "$expect_bad"; then
		bad_counter nf_bad "$expect_bad" "$what"
		lret=1
	fi

	if [ $lret -eq 0 ]; then
		echo "PASS: $what"
	else
		ret=1
	fi
}

scenario_single_nh() {
	ip -net "$nsrouter" nexthop add id 1 via dead:1::99 dev veth0
	ip -net "$nsrouter" -6 route add dead:dead::/64 nhid 1
}
run_scenario "single external nexthop (nhid -> veth0)" scenario_single_nh

scenario_nh_group() {
	ip -net "$nsrouter" nexthop add id 1   via dead:2::2  dev dummy0
	ip -net "$nsrouter" nexthop add id 2   via dead:1::99 dev veth0
	ip -net "$nsrouter" nexthop add id 100 group 1/2
	ip -net "$nsrouter" -6 route   add dead:dead::/64 nhid 100
}
run_scenario "nexthop group (dummy0 + veth0)" scenario_nh_group

scenario_old_multipath() {
	ip -net "$nsrouter" -6 route add dead:dead::/64 \
		nexthop via dead:2::2  dev dummy0 \
		nexthop via dead:1::99 dev veth0
}
run_scenario "old-style multipath (sibling on veth0)" scenario_old_multipath

exit $ret

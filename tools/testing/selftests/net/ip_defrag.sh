#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Run a couple of IP defragmentation tests.

set +x
set -e

readonly NETNS="ns-$(mktemp -u XXXXXX)"

setup() {
	ip netns add "${NETNS}"
	ip -netns "${NETNS}" link set lo up

	ip netns exec "${NETNS}" sysctl -w net.ipv4.ipfrag_high_thresh=9000000 >/dev/null 2>&1
	ip netns exec "${NETNS}" sysctl -w net.ipv4.ipfrag_low_thresh=7000000 >/dev/null 2>&1
	ip netns exec "${NETNS}" sysctl -w net.ipv4.ipfrag_time=1 >/dev/null 2>&1

	ip netns exec "${NETNS}" sysctl -w net.ipv6.ip6frag_high_thresh=9000000 >/dev/null 2>&1
	ip netns exec "${NETNS}" sysctl -w net.ipv6.ip6frag_low_thresh=7000000 >/dev/null 2>&1
	ip netns exec "${NETNS}" sysctl -w net.ipv6.ip6frag_time=1 >/dev/null 2>&1

	ip netns exec "${NETNS}" sysctl -w net.netfilter.nf_conntrack_frag6_high_thresh=9000000 >/dev/null 2>&1
	ip netns exec "${NETNS}" sysctl -w net.netfilter.nf_conntrack_frag6_low_thresh=7000000  >/dev/null 2>&1
	ip netns exec "${NETNS}" sysctl -w net.netfilter.nf_conntrack_frag6_timeout=1 >/dev/null 2>&1

	# DST cache can get full with a lot of frags, with GC not keeping up with the test.
	ip netns exec "${NETNS}" sysctl -w net.ipv6.route.max_size=65536 >/dev/null 2>&1
}

cleanup() {
	ip netns del "${NETNS}"
}

trap cleanup EXIT
setup

echo "ipv4 defrag"
ip netns exec "${NETNS}" ./ip_defrag -4

echo "ipv4 defrag with overlaps"
ip netns exec "${NETNS}" ./ip_defrag -4o

echo "ipv6 defrag"
ip netns exec "${NETNS}" ./ip_defrag -6

echo "ipv6 defrag with overlaps"
ip netns exec "${NETNS}" ./ip_defrag -6o

# insert an nf_conntrack rule so that the codepath in nf_conntrack_reasm.c taken
ip netns exec "${NETNS}" ip6tables -A INPUT  -m conntrack --ctstate INVALID -j ACCEPT

echo "ipv6 nf_conntrack defrag"
ip netns exec "${NETNS}" ./ip_defrag -6

echo "ipv6 nf_conntrack defrag with overlaps"
# netfilter will drop some invalid packets, so we run the test in
# permissive mode: i.e. pass the test if the packet is correctly assembled
# even if we sent an overlap
ip netns exec "${NETNS}" ./ip_defrag -6op

echo "all tests done"

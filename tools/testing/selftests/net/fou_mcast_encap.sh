#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that UDP encapsulation (FOU) correctly handles packet resubmit
# when packets are delivered via the multicast UDP delivery path.
#
# When a FOU-encapsulated packet arrives with a multicast destination IP,
# __udp4_lib_mcast_deliver() / __udp6_lib_mcast_deliver() must resubmit
# it to the inner protocol handler (e.g., GRE) rather than consuming it.
# This test verifies both IPv4 and IPv6 paths by creating a FOU/GRETAP
# tunnel with a multicast remote address and sending ping through it.
#
# The early demux optimization can mask this issue by routing packets via
# the unicast path (udp[6]_unicast_rcv_skb), so we disable it to force
# packets through the multicast delivery function.

source lib.sh

NSENDER=""
NRECV=""

FOU_PORT4=4797
FOU_PORT6=4798
MCAST4=239.0.0.1
MCAST6=ff0e::1

TUN4_S=192.168.99.1
TUN4_R=192.168.99.2
TUN6_S=2001:db8:99::1
TUN6_R=2001:db8:99::2

cleanup() {
	cleanup_all_ns
}

trap cleanup EXIT

setup_common() {
	setup_ns NSENDER NRECV

	# Create veth pair directly inside namespaces to avoid name
	# collisions with devices in the root namespace.
	ip link add veth_s netns "$NSENDER" type veth \
		peer name veth_r netns "$NRECV"

	ip -n "$NSENDER" link set veth_s up
	ip -n "$NRECV" link set veth_r up

	# Same sysctl controls early demux for both IPv4 and IPv6.
	ip netns exec "$NRECV" sysctl -wq net.ipv4.ip_early_demux=0
}

setup_ipv4() {
	# IPv4 FOU (CONFIG_NET_FOU) is built in on kernels configured for
	# these tests, so no module load is needed here.
	ip -n "$NSENDER" addr add 10.0.0.1/24 dev veth_s
	ip -n "$NRECV" addr add 10.0.0.2/24 dev veth_r

	# Join multicast group on receiver
	ip -n "$NRECV" addr add "$MCAST4/32" dev veth_r autojoin

	ip -n "$NSENDER" route add 239.0.0.0/8 dev veth_s
	ip -n "$NRECV" route add 239.0.0.0/8 dev veth_r

	# Sender: GRETAP with FOU encap (no FOU listener needed on TX side)
	ip -n "$NSENDER" link add eoudp4 type gretap \
		remote "$MCAST4" local 10.0.0.1 \
		encap fou encap-sport "$FOU_PORT4" encap-dport "$FOU_PORT4" \
		key "$MCAST4"
	ip -n "$NSENDER" link set eoudp4 up
	ip -n "$NSENDER" addr add "$TUN4_S/24" dev eoudp4

	# Receiver: FOU listener + GRETAP
	ip netns exec "$NRECV" ip fou add port "$FOU_PORT4" ipproto 47
	ip -n "$NRECV" link add eoudp4 type gretap \
		remote "$MCAST4" local 10.0.0.2 \
		encap fou encap-sport "$FOU_PORT4" encap-dport "$FOU_PORT4" \
		key "$MCAST4"
	ip -n "$NRECV" link set eoudp4 up
	ip -n "$NRECV" addr add "$TUN4_R/24" dev eoudp4

	# Static neigh on sender: ARP replies cannot traverse the
	# unidirectional multicast tunnel.
	local recv_mac
	recv_mac=$(ip -n "$NRECV" link show eoudp4 | awk '/ether/{print $2}')
	ip -n "$NSENDER" neigh add "$TUN4_R" lladdr "$recv_mac" dev eoudp4
}

setup_ipv6() {
	# Skip cleanly if IPv6 or the fou6 module is not available.
	[ -e /proc/sys/net/ipv6 ] || return "$ksft_skip"
	modprobe -q fou6 || return "$ksft_skip"

	ip -n "$NSENDER" addr add 2001:db8::1/64 dev veth_s nodad
	ip -n "$NRECV" addr add 2001:db8::2/64 dev veth_r nodad

	# Join multicast group on receiver
	ip -n "$NRECV" addr add "$MCAST6/128" dev veth_r autojoin

	ip -n "$NSENDER" -6 route add ff00::/8 dev veth_s
	ip -n "$NRECV" -6 route add ff00::/8 dev veth_r

	# Sender: ip6gretap with FOU encap
	ip -n "$NSENDER" link add eoudp6 type ip6gretap \
		remote "$MCAST6" local 2001:db8::1 \
		encap fou encap-sport "$FOU_PORT6" encap-dport "$FOU_PORT6" \
		key 42
	ip -n "$NSENDER" link set eoudp6 up
	ip -n "$NSENDER" addr add "$TUN6_S/64" dev eoudp6 nodad

	# Receiver: FOU listener (IPv6) + ip6gretap
	ip netns exec "$NRECV" ip fou add port "$FOU_PORT6" ipproto 47 -6
	ip -n "$NRECV" link add eoudp6 type ip6gretap \
		remote "$MCAST6" local 2001:db8::2 \
		encap fou encap-sport "$FOU_PORT6" encap-dport "$FOU_PORT6" \
		key 42
	ip -n "$NRECV" link set eoudp6 up
	ip -n "$NRECV" addr add "$TUN6_R/64" dev eoudp6 nodad

	# Static neigh on sender: neighbor discovery cannot traverse the
	# unidirectional multicast tunnel.
	local recv_mac
	recv_mac=$(ip -n "$NRECV" link show eoudp6 | awk '/ether/{print $2}')
	ip -n "$NSENDER" neigh add "$TUN6_R" lladdr "$recv_mac" dev eoudp6
}

get_rx_packets() {
	local dev="$1"

	ip -n "$NRECV" -s link show "$dev" | awk '/RX:/{getline; print $2}'
}

run_ping_test() {
	local family="$1"
	local dev="$2"
	local dst="$3"
	local name="$4"
	local count=100
	local rx_before rx_after rx_delta

	# Warmup: let any initial broadcast/ND traffic settle
	ip netns exec "$NSENDER" ping "$family" -c 1 -W 1 "$dst" \
		>/dev/null 2>&1
	sleep 1

	rx_before=$(get_rx_packets "$dev")
	ip netns exec "$NSENDER" ping "$family" -i 0.01 -c $count -W 1 "$dst" \
		>/dev/null 2>&1
	sleep 1
	rx_after=$(get_rx_packets "$dev")

	rx_delta=$((rx_after - rx_before))

	if [ "$rx_delta" -ge "$count" ]; then
		RET=$ksft_pass
	else
		RET=$ksft_fail
	fi
	log_test "$name (received $rx_delta/$count)"
}

setup_common
setup_ipv4
run_ping_test -4 eoudp4 "$TUN4_R" "FOU/GRETAP IPv4 multicast encap resubmit"

if setup_ipv6; then
	run_ping_test -6 eoudp6 "$TUN6_R" "FOU/ip6gretap IPv6 multicast encap resubmit"
else
	log_test_skip "FOU/ip6gretap IPv6 multicast encap resubmit"
fi

exit "$EXIT_STATUS"

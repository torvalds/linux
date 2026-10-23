#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that GRE packets carried over UDP (FOU or GUE) get through when the
# receiver merges them with GRO.
#
# Send bulk TCP through such tunnels with GRO on at the receiver, and
# segmentation off at the sender so the receiver gets wire-sized packets
# to merge, and check that the receiver counts no UDP errors (UdpInErrors
# or Udp6InErrors). Also check that GRO really merged packets, by comparing
# the UDP datagrams the receiver delivered with the packets its veth
# received, so that a setup where GRO doesn't engage skips rather than
# passes.

source lib.sh

NSEND=""
NRECV=""

FOU_PORT=5555
TCP_PORT=9000
SIZE_MB=4

TUN_S=192.168.99.1
TUN_R=192.168.99.2

cleanup() {
	cleanup_all_ns
}

trap cleanup EXIT

# setup_tunnel creates the namespaces, the veth between them and a GRE
# tunnel over UDP encapsulation $1 (fou or gue) over IPv$2. It returns
# $ksft_skip if the kernel or iproute2 can't set it up.
setup_tunnel() {
	local encap=$1
	local family=$2
	local type=gre
	local addr_s=10.0.0.1
	local addr_r=10.0.0.2
	local fou_args="ipproto 47"

	if [ "$encap" = gue ]; then
		fou_args=gue
	fi
	if [ "$family" = 6 ]; then
		type=ip6gre
		addr_s=2001:db8::1
		addr_r=2001:db8::2
		fou_args="$fou_args -6"
	fi

	setup_ns NSEND NRECV || return "$ksft_skip"

	ip link add veth_s netns "$NSEND" type veth \
		peer name veth_r netns "$NRECV" || return "$ksft_skip"
	ip -n "$NSEND" addr add 10.0.0.1/24 dev veth_s
	ip -n "$NRECV" addr add 10.0.0.2/24 dev veth_r
	ip -n "$NSEND" addr add 2001:db8::1/64 dev veth_s nodad
	ip -n "$NRECV" addr add 2001:db8::2/64 dev veth_r nodad
	ip -n "$NSEND" link set veth_s up
	ip -n "$NRECV" link set veth_r up

	ip netns exec "$NRECV" ethtool -K veth_r gro on || return "$ksft_skip"
	ip netns exec "$NSEND" ethtool -K veth_s tso off gso off \
		tx-gre-segmentation off tx-gre-csum-segmentation off \
		tx-udp_tnl-segmentation off tx-udp_tnl-csum-segmentation off ||
		return "$ksft_skip"

	# shellcheck disable=SC2086 # $fou_args is several arguments
	ip netns exec "$NSEND" ip fou add port "$FOU_PORT" $fou_args ||
		return "$ksft_skip"
	# shellcheck disable=SC2086
	ip netns exec "$NRECV" ip fou add port "$FOU_PORT" $fou_args ||
		return "$ksft_skip"
	ip -n "$NSEND" link add gre_t type "$type" \
		local "$addr_s" remote "$addr_r" \
		encap "$encap" encap-sport auto encap-dport "$FOU_PORT" ||
		return "$ksft_skip"
	ip -n "$NRECV" link add gre_t type "$type" \
		local "$addr_r" remote "$addr_s" \
		encap "$encap" encap-sport auto encap-dport "$FOU_PORT" ||
		return "$ksft_skip"

	ip -n "$NSEND" addr add "$TUN_S/30" dev gre_t
	ip -n "$NRECV" addr add "$TUN_R/30" dev gre_t
	ip -n "$NSEND" link set gre_t up
	ip -n "$NRECV" link set gre_t up
}

# recv_counter prints the sum of the receiver's nstat counters "$@".
recv_counter() {
	ip netns exec "$NRECV" nstat -az "$@" |
		awk '!/^#/ { n += $2 } END { print n + 0 }'
}

run_test() {
	local encap=$1
	local family=$2
	local name="GRO of GRE in ${encap^^} over IPv$family"
	local datagrams
	local packets
	local errors
	local pid
	local rc

	RET=0

	if ! setup_tunnel "$encap" "$family"; then
		log_test_skip "$name"
		cleanup_all_ns
		return
	fi

	ip netns exec "$NRECV" socat -u \
		TCP4-LISTEN:"$TCP_PORT",bind="$TUN_R",reuseaddr OPEN:/dev/null &
	pid=$!
	wait_local_port_listen "$NRECV" "$TCP_PORT" tcp

	# The timeout only bounds failing runs; passing ones take under a second.
	dd if=/dev/zero bs=1M count="$SIZE_MB" 2>/dev/null |
		ip netns exec "$NSEND" timeout 20 \
			socat -u STDIN TCP4:"$TUN_R":"$TCP_PORT"
	rc=$?

	# Check the UDP error count rather than the duration, which depends
	# on the machine.
	errors=$(recv_counter UdpInErrors Udp6InErrors)
	check_err "$errors" "$errors UDP errors at the receiver"
	check_err "$rc" "transfer failed"

	# Without errors, a pass only means something if GRO merged packets:
	# then the encapsulation socket sees far fewer datagrams than the
	# veth received packets.
	datagrams=$(recv_counter UdpInDatagrams Udp6InDatagrams)
	packets=$(ip netns exec "$NRECV" \
		cat /sys/class/net/veth_r/statistics/rx_packets)
	if [ "$RET" -eq 0 ] && [ $((datagrams * 2)) -gt "$packets" ]; then
		ret_set_ksft_status "$ksft_skip" \
			"GRO didn't merge: $datagrams UDP datagrams from $packets packets"
	fi

	log_test "$name"
	kill "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
	cleanup_all_ns
}

require_command socat
require_command ethtool
require_command nstat

modprobe -q fou
modprobe -q ip_gre
modprobe -q fou6
modprobe -q ip6_gre

run_test fou 4
run_test gue 4
run_test fou 6
run_test gue 6

exit "$EXIT_STATUS"

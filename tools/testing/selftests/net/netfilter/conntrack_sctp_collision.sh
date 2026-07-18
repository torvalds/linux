#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing For SCTP COLLISION SCENARIO as Below:
# 1. Stale INIT_ACK capture:
#   14:35:47.655279 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [INIT] [init tag: 2017837359]
#   14:35:48.353250 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [INIT] [init tag: 1187206187]
#   14:35:48.353275 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [INIT ACK] [init tag: 2017837359]
#   14:35:48.353283 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [COOKIE ECHO]
#   14:35:48.353977 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [COOKIE ACK]
#   14:35:48.855335 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [INIT ACK] [init tag: 164579970]
#   (Delayed)
#
# 2. Stale INIT capture:
#   14:35:48.353250 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [INIT] [init tag: 1187206187]
#   14:35:48.353275 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [INIT ACK] [init tag: 2017837359]
#   14:35:48.353283 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [COOKIE ECHO]
#   14:35:48.353977 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [COOKIE ACK]
#   14:35:47.655279 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [INIT] [init tag: 2017837359]
#   (Delayed)
#   14:35:48.855335 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [INIT ACK] [init tag: 164579970]
#
# TOPO: SERVER_NS (link0)<--->(link1) ROUTER_NS (link2)<--->(link3) CLIENT_NS

source lib.sh

checktool "nft --version" "run test without nft"
checktool "tc -h" "run test without tc"
checktool "modprobe -q sctp" "load sctp module"

CLIENT_IP="198.51.200.1"
CLIENT_PORT=1234

SERVER_IP="198.51.100.1"
SERVER_PORT=1234

CLIENT_GW="198.51.200.2"
SERVER_GW="198.51.100.2"

# setup the topo
topo_setup() {
	# setup_ns cleans up existing net namespaces first.
	setup_ns CLIENT_NS SERVER_NS ROUTER_NS
	ip -n "$SERVER_NS" link add link0 type veth peer name link1 netns "$ROUTER_NS"
	ip -n "$CLIENT_NS" link add link3 type veth peer name link2 netns "$ROUTER_NS"

	ip -n "$SERVER_NS" link set link0 up
	ip -n "$SERVER_NS" addr add $SERVER_IP/24 dev link0
	ip -n "$SERVER_NS" route add $CLIENT_IP dev link0 via $SERVER_GW

	ip -n "$ROUTER_NS" link set link1 up
	ip -n "$ROUTER_NS" link set link2 up
	ip -n "$ROUTER_NS" addr add $SERVER_GW/24 dev link1
	ip -n "$ROUTER_NS" addr add $CLIENT_GW/24 dev link2
	ip net exec "$ROUTER_NS" sysctl -wq net.ipv4.ip_forward=1
	sysctl -wq net.netfilter.nf_log_all_netns=1

	ip -n "$CLIENT_NS" link set link3 up
	ip -n "$CLIENT_NS" addr add $CLIENT_IP/24 dev link3
	ip -n "$CLIENT_NS" route add $SERVER_IP dev link3 via $CLIENT_GW
}

conf_delay()
{
	# simulate the delay on OVS upcall by setting up a delay for INIT_ACK/INIT with
	local ns=$1
	local link=$2
	local chunk_type=$3

	# use a smaller number for assoc's max_retrans to reproduce the issue
	ip net exec "$CLIENT_NS" sysctl -wq net.sctp.association_max_retrans=3

	tc -n "$ns" qdisc add dev "$link" root handle 1: htb r2q 64
	tc -n "$ns" class add dev "$link" parent 1: classid 1:1 htb rate 100mbit
	tc -n "$ns" filter add dev "$link" parent 1: protocol ip \
		u32 match ip protocol 132 0xff match u8 "$chunk_type" 0xff at 32 flowid 1:1
	if ! tc -n "$ns" qdisc add dev "$link" parent 1:1 handle 10: netem delay 1200ms; then
		echo "SKIP: Cannot add netem qdisc"
		return $ksft_skip
	fi

	# simulate the ctstate check on OVS nf_conntrack
	ip net exec "$ROUTER_NS" nft -f - <<-EOF
	table ip t {
		chain forward {
			type filter hook forward priority filter; policy accept;
			meta l4proto icmp counter accept
			ct state new counter accept
			ct state established,related counter accept
			ct state invalid log flags all counter drop comment \
			"Expect to drop stale INIT/INIT_ACK chunks"
			counter
		}
	}
	EOF
	return 0
}

cleanup() {
	# cleanup_all_ns terminates running processes in the namespaces.
	cleanup_all_ns
	sysctl -wq net.netfilter.nf_log_all_netns=0
}

do_test() {
	ip net exec "$SERVER_NS" ./sctp_collision server \
		$SERVER_IP $SERVER_PORT $CLIENT_IP $CLIENT_PORT &
	ip net exec "$CLIENT_NS" ./sctp_collision client \
		$CLIENT_IP $CLIENT_PORT $SERVER_IP $SERVER_PORT
}

# NOTE: one way to work around the issue is set a smaller hb_interval
# ip net exec $CLIENT_NS sysctl -wq net.sctp.hb_interval=3500

# run the test case
trap cleanup EXIT

echo "Test for SCTP INIT_ACK Collision in nf_conntrack:"
topo_setup || exit $?
conf_delay $SERVER_NS link0 2 || exit $?

if ! do_test; then
	exit $ksft_fail
fi

echo "Test for SCTP INIT Collision in nf_conntrack:"
topo_setup || exit $?
conf_delay $CLIENT_NS link3 1 || exit $?

if ! do_test; then
	exit $ksft_fail
fi

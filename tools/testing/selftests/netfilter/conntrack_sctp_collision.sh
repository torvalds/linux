#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing For SCTP COLLISION SCENARIO as Below:
#
#   14:35:47.655279 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [INIT] [init tag: 2017837359]
#   14:35:48.353250 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [INIT] [init tag: 1187206187]
#   14:35:48.353275 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [INIT ACK] [init tag: 2017837359]
#   14:35:48.353283 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [COOKIE ECHO]
#   14:35:48.353977 IP CLIENT_IP.PORT > SERVER_IP.PORT: sctp (1) [COOKIE ACK]
#   14:35:48.855335 IP SERVER_IP.PORT > CLIENT_IP.PORT: sctp (1) [INIT ACK] [init tag: 164579970]
#
# TOPO: SERVER_NS (link0)<--->(link1) ROUTER_NS (link2)<--->(link3) CLIENT_NS

CLIENT_NS=$(mktemp -u client-XXXXXXXX)
CLIENT_IP="198.51.200.1"
CLIENT_PORT=1234

SERVER_NS=$(mktemp -u server-XXXXXXXX)
SERVER_IP="198.51.100.1"
SERVER_PORT=1234

ROUTER_NS=$(mktemp -u router-XXXXXXXX)
CLIENT_GW="198.51.200.2"
SERVER_GW="198.51.100.2"

# setup the topo
setup() {
	ip net add $CLIENT_NS
	ip net add $SERVER_NS
	ip net add $ROUTER_NS
	ip -n $SERVER_NS link add link0 type veth peer name link1 netns $ROUTER_NS
	ip -n $CLIENT_NS link add link3 type veth peer name link2 netns $ROUTER_NS

	ip -n $SERVER_NS link set link0 up
	ip -n $SERVER_NS addr add $SERVER_IP/24 dev link0
	ip -n $SERVER_NS route add $CLIENT_IP dev link0 via $SERVER_GW

	ip -n $ROUTER_NS link set link1 up
	ip -n $ROUTER_NS link set link2 up
	ip -n $ROUTER_NS addr add $SERVER_GW/24 dev link1
	ip -n $ROUTER_NS addr add $CLIENT_GW/24 dev link2
	ip net exec $ROUTER_NS sysctl -wq net.ipv4.ip_forward=1

	ip -n $CLIENT_NS link set link3 up
	ip -n $CLIENT_NS addr add $CLIENT_IP/24 dev link3
	ip -n $CLIENT_NS route add $SERVER_IP dev link3 via $CLIENT_GW

	# simulate the delay on OVS upcall by setting up a delay for INIT_ACK with
	# tc on $SERVER_NS side
	tc -n $SERVER_NS qdisc add dev link0 root handle 1: htb
	tc -n $SERVER_NS class add dev link0 parent 1: classid 1:1 htb rate 100mbit
	tc -n $SERVER_NS filter add dev link0 parent 1: protocol ip u32 match ip protocol 132 \
		0xff match u8 2 0xff at 32 flowid 1:1
	tc -n $SERVER_NS qdisc add dev link0 parent 1:1 handle 10: netem delay 1200ms

	# simulate the ctstate check on OVS nf_conntrack
	ip net exec $ROUTER_NS iptables -A FORWARD -m state --state INVALID,UNTRACKED -j DROP
	ip net exec $ROUTER_NS iptables -A INPUT -p sctp -j DROP

	# use a smaller number for assoc's max_retrans to reproduce the issue
	modprobe sctp
	ip net exec $CLIENT_NS sysctl -wq net.sctp.association_max_retrans=3
}

cleanup() {
	ip net exec $CLIENT_NS pkill sctp_collision 2>&1 >/dev/null
	ip net exec $SERVER_NS pkill sctp_collision 2>&1 >/dev/null
	ip net del "$CLIENT_NS"
	ip net del "$SERVER_NS"
	ip net del "$ROUTER_NS"
}

do_test() {
	ip net exec $SERVER_NS ./sctp_collision server \
		$SERVER_IP $SERVER_PORT $CLIENT_IP $CLIENT_PORT &
	ip net exec $CLIENT_NS ./sctp_collision client \
		$CLIENT_IP $CLIENT_PORT $SERVER_IP $SERVER_PORT
}

# NOTE: one way to work around the issue is set a smaller hb_interval
# ip net exec $CLIENT_NS sysctl -wq net.sctp.hb_interval=3500

# run the test case
trap cleanup EXIT
setup && \
echo "Test for SCTP Collision in nf_conntrack:" && \
do_test && echo "PASS!"
exit $?

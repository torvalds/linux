#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing For IPv4 and IPv6 BIG TCP.
# TOPO: CLIENT_NS (link0)<--->(link1) ROUTER_NS (link2)<--->(link3) SERVER_NS

CLIENT_NS=$(mktemp -u client-XXXXXXXX)
CLIENT_IP4="198.51.100.1"
CLIENT_IP6="2001:db8:1::1"

SERVER_NS=$(mktemp -u server-XXXXXXXX)
SERVER_IP4="203.0.113.1"
SERVER_IP6="2001:db8:2::1"

ROUTER_NS=$(mktemp -u router-XXXXXXXX)
SERVER_GW4="203.0.113.2"
CLIENT_GW4="198.51.100.2"
SERVER_GW6="2001:db8:2::2"
CLIENT_GW6="2001:db8:1::2"

MAX_SIZE=128000
CHK_SIZE=65535

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

setup() {
	ip netns add $CLIENT_NS
	ip netns add $SERVER_NS
	ip netns add $ROUTER_NS
	ip -net $ROUTER_NS link add link1 type veth peer name link0 netns $CLIENT_NS
	ip -net $ROUTER_NS link add link2 type veth peer name link3 netns $SERVER_NS

	ip -net $CLIENT_NS link set link0 up
	ip -net $CLIENT_NS link set link0 mtu 1442
	ip -net $CLIENT_NS addr add $CLIENT_IP4/24 dev link0
	ip -net $CLIENT_NS addr add $CLIENT_IP6/64 dev link0 nodad
	ip -net $CLIENT_NS route add $SERVER_IP4 dev link0 via $CLIENT_GW4
	ip -net $CLIENT_NS route add $SERVER_IP6 dev link0 via $CLIENT_GW6
	ip -net $CLIENT_NS link set dev link0 \
		gro_ipv4_max_size $MAX_SIZE gso_ipv4_max_size $MAX_SIZE
	ip -net $CLIENT_NS link set dev link0 \
		gro_max_size $MAX_SIZE gso_max_size $MAX_SIZE
	ip net exec $CLIENT_NS sysctl -wq net.ipv4.tcp_window_scaling=10

	ip -net $ROUTER_NS link set link1 up
	ip -net $ROUTER_NS link set link2 up
	ip -net $ROUTER_NS addr add $CLIENT_GW4/24 dev link1
	ip -net $ROUTER_NS addr add $CLIENT_GW6/64 dev link1 nodad
	ip -net $ROUTER_NS addr add $SERVER_GW4/24 dev link2
	ip -net $ROUTER_NS addr add $SERVER_GW6/64 dev link2 nodad
	ip -net $ROUTER_NS link set dev link1 \
		gro_ipv4_max_size $MAX_SIZE gso_ipv4_max_size $MAX_SIZE
	ip -net $ROUTER_NS link set dev link2 \
		gro_ipv4_max_size $MAX_SIZE gso_ipv4_max_size $MAX_SIZE
	ip -net $ROUTER_NS link set dev link1 \
		gro_max_size $MAX_SIZE gso_max_size $MAX_SIZE
	ip -net $ROUTER_NS link set dev link2 \
		gro_max_size $MAX_SIZE gso_max_size $MAX_SIZE
	# test for nf_ct_skb_network_trim in nf_conntrack_ovs used by TC ct action.
	ip net exec $ROUTER_NS tc qdisc add dev link1 ingress
	ip net exec $ROUTER_NS tc filter add dev link1 ingress \
		proto ip flower ip_proto tcp action ct
	ip net exec $ROUTER_NS tc filter add dev link1 ingress \
		proto ipv6 flower ip_proto tcp action ct
	ip net exec $ROUTER_NS sysctl -wq net.ipv4.ip_forward=1
	ip net exec $ROUTER_NS sysctl -wq net.ipv6.conf.all.forwarding=1

	ip -net $SERVER_NS link set link3 up
	ip -net $SERVER_NS addr add $SERVER_IP4/24 dev link3
	ip -net $SERVER_NS addr add $SERVER_IP6/64 dev link3 nodad
	ip -net $SERVER_NS route add $CLIENT_IP4 dev link3 via $SERVER_GW4
	ip -net $SERVER_NS route add $CLIENT_IP6 dev link3 via $SERVER_GW6
	ip -net $SERVER_NS link set dev link3 \
		gro_ipv4_max_size $MAX_SIZE gso_ipv4_max_size $MAX_SIZE
	ip -net $SERVER_NS link set dev link3 \
		gro_max_size $MAX_SIZE gso_max_size $MAX_SIZE
	ip net exec $SERVER_NS sysctl -wq net.ipv4.tcp_window_scaling=10
	ip net exec $SERVER_NS netserver 2>&1 >/dev/null
}

cleanup() {
	ip net exec $SERVER_NS pkill netserver
	ip -net $ROUTER_NS link del link1
	ip -net $ROUTER_NS link del link2
	ip netns del "$CLIENT_NS"
	ip netns del "$SERVER_NS"
	ip netns del "$ROUTER_NS"
}

start_counter() {
	local ipt="iptables"
	local iface=$1
	local netns=$2

	[ "$NF" = "6" ] && ipt="ip6tables"
	ip net exec $netns $ipt -t raw -A PREROUTING -i $iface \
		-m length ! --length 0:$CHK_SIZE -j ACCEPT
}

check_counter() {
	local ipt="iptables"
	local iface=$1
	local netns=$2

	[ "$NF" = "6" ] && ipt="ip6tables"
	test `ip net exec $netns $ipt -t raw -L -v |grep $iface | awk '{print $1}'` != "0"
}

stop_counter() {
	local ipt="iptables"
	local iface=$1
	local netns=$2

	[ "$NF" = "6" ] && ipt="ip6tables"
	ip net exec $netns $ipt -t raw -D PREROUTING -i $iface \
		-m length ! --length 0:$CHK_SIZE -j ACCEPT
}

do_netperf() {
	local serip=$SERVER_IP4
	local netns=$1

	[ "$NF" = "6" ] && serip=$SERVER_IP6
	ip net exec $netns netperf -$NF -t TCP_STREAM -H $serip 2>&1 >/dev/null
}

do_test() {
	local cli_tso=$1
	local gw_gro=$2
	local gw_tso=$3
	local ser_gro=$4
	local ret="PASS"

	ip net exec $CLIENT_NS ethtool -K link0 tso $cli_tso
	ip net exec $ROUTER_NS ethtool -K link1 gro $gw_gro
	ip net exec $ROUTER_NS ethtool -K link2 tso $gw_tso
	ip net exec $SERVER_NS ethtool -K link3 gro $ser_gro

	start_counter link1 $ROUTER_NS
	start_counter link3 $SERVER_NS
	do_netperf $CLIENT_NS

	if check_counter link1 $ROUTER_NS; then
		check_counter link3 $SERVER_NS || ret="FAIL_on_link3"
	else
		ret="FAIL_on_link1"
	fi

	stop_counter link1 $ROUTER_NS
	stop_counter link3 $SERVER_NS
	printf "%-9s %-8s %-8s %-8s: [%s]\n" \
		$cli_tso $gw_gro $gw_tso $ser_gro $ret
	test $ret = "PASS"
}

testup() {
	echo "CLI GSO | GW GRO | GW GSO | SER GRO" && \
	do_test "on"  "on"  "on"  "on"  && \
	do_test "on"  "off" "on"  "off" && \
	do_test "off" "on"  "on"  "on"  && \
	do_test "on"  "on"  "off" "on"  && \
	do_test "off" "on"  "off" "on"
}

if ! netperf -V &> /dev/null; then
	echo "SKIP: Could not run test without netperf tool"
	exit $ksft_skip
fi

if ! ip link help 2>&1 | grep gso_ipv4_max_size &> /dev/null; then
	echo "SKIP: Could not run test without gso/gro_ipv4_max_size supported in ip-link"
	exit $ksft_skip
fi

trap cleanup EXIT
setup && echo "Testing for BIG TCP:" && \
NF=4 testup && echo "***v4 Tests Done***" && \
NF=6 testup && echo "***v6 Tests Done***"
exit $?

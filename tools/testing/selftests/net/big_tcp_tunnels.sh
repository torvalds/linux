#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Testing for IPv4 and IPv6 BIG TCP over VXLAN and GENEVE tunnels.

SERVER_NS=$(mktemp -u server-XXXXXXXX)
SERVER_IP4="192.168.1.1"
SERVER_IP6="2001:db8::1:1"
SERVER_IP4_TUN="192.168.2.1"
SERVER_IP6_TUN="2001:db8::2:1"

CLIENT_NS=$(mktemp -u client-XXXXXXXX)
CLIENT_IP4="192.168.1.2"
CLIENT_IP6="2001:db8::1:2"
CLIENT_IP4_TUN="192.168.2.2"
CLIENT_IP6_TUN="2001:db8::2:2"

: "${PACKETS_THRESHOLD:=1000}"

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

setup() {
	ip netns add "$SERVER_NS"
	ip netns add "$CLIENT_NS"
	ip -netns "$SERVER_NS" link add link1 type veth peer name link0 netns "$CLIENT_NS"

	ip -netns "$CLIENT_NS" link set link0 up
	ip -netns "$CLIENT_NS" addr replace "$CLIENT_IP4/24" dev link0
	ip -netns "$CLIENT_NS" addr replace "$CLIENT_IP6/112" dev link0 nodad
	ip -netns "$CLIENT_NS" link set link0 \
		gso_max_size 196608 gso_ipv4_max_size 196608 \
		gro_max_size 196608 gro_ipv4_max_size 196608
	ip -netns "$SERVER_NS" link set link1 up
	ip -netns "$SERVER_NS" addr replace "$SERVER_IP4/24" dev link1
	ip -netns "$SERVER_NS" addr replace "$SERVER_IP6/112" dev link1 nodad
	ip -netns "$SERVER_NS" link set link1 \
		gso_max_size 196608 gso_ipv4_max_size 196608 \
		gro_max_size 196608 gro_ipv4_max_size 196608

	ip netns exec "$SERVER_NS" netserver >/dev/null
}

setup_tunnel() {
	if [ "$2" = 4 ]; then
		SERVER_IP="$SERVER_IP4"
		CLIENT_IP="$CLIENT_IP4"
		echo "Setting up ${1^^} over IPv4, veth tx csum offload $3"
	else
		SERVER_IP="$SERVER_IP6"
		CLIENT_IP="$CLIENT_IP6"
		echo "Setting up ${1^^} over IPv6, veth tx csum offload $3"
	fi

	if [ "$1" = vxlan ]; then
		ip -netns "$CLIENT_NS" link add tun0 type vxlan \
			id 5001 remote "$SERVER_IP" local "$CLIENT_IP" dev link0 dstport 4789
	else
		ip -netns "$CLIENT_NS" link add tun0 type geneve \
			id 5001 remote "$SERVER_IP"
	fi
	ip -netns "$CLIENT_NS" link set tun0 up
	ip -netns "$CLIENT_NS" addr replace "$CLIENT_IP4_TUN/24" dev tun0
	ip -netns "$CLIENT_NS" addr replace "$CLIENT_IP6_TUN/112" dev tun0 nodad
	ip -netns "$CLIENT_NS" link set tun0 \
		gso_max_size 196608 gso_ipv4_max_size 196608 \
		gro_max_size 196608 gro_ipv4_max_size 196608
	if [ "$1" = vxlan ]; then
		ip -netns "$SERVER_NS" link add tun1 type vxlan \
			id 5001 remote "$CLIENT_IP" local "$SERVER_IP" dev link1 dstport 4789
	else
		ip -netns "$SERVER_NS" link add tun1 type geneve \
			id 5001 remote "$CLIENT_IP"
	fi
	ip -netns "$SERVER_NS" link set tun1 up
	ip -netns "$SERVER_NS" addr replace "$SERVER_IP4_TUN/24" dev tun1
	ip -netns "$SERVER_NS" addr replace "$SERVER_IP6_TUN/112" dev tun1 nodad
	ip -netns "$SERVER_NS" link set tun1 \
		gso_max_size 196608 gso_ipv4_max_size 196608 \
		gro_max_size 196608 gro_ipv4_max_size 196608

	ip netns exec "$CLIENT_NS" ethtool -K link0 tx-checksumming "$3" > /dev/null
	ip netns exec "$SERVER_NS" ethtool -K link1 tx-checksumming "$3" > /dev/null
}

cleanup_tunnel() {
	ip -netns "$CLIENT_NS" link del tun0
	ip -netns "$SERVER_NS" link del tun1
}

cleanup() {
	ip netns pids "$SERVER_NS" | xargs -r kill
	ip netns pids "$CLIENT_NS" | xargs -r kill
	ip netns del "$SERVER_NS"
	ip netns del "$CLIENT_NS"
	rm -rf "$WORKDIR"
}

do_test() {
	# When tx csum offload is off, software GSO is performed before passing the
	# packet to veth. Check BIG TCP packets inside the VXLAN tunnel to verify
	# the software checksum path: if the checksum code is broken, these packets
	# will be dropped.
	if [ "$3" = on ]; then
		CAPTURE_IFACE='link'
		if [ "$1" = 4 ]; then
			IPTABLES=iptables
		else
			IPTABLES=ip6tables
		fi
	else
		CAPTURE_IFACE='tun'
		if [ "$2" = 4 ]; then
			IPTABLES=iptables
		else
			IPTABLES=ip6tables
		fi
	fi
	if [ "$2" = 4 ]; then
		IPTABLES_SACK=iptables
	else
		IPTABLES_SACK=ip6tables
	fi

	ip netns exec "$SERVER_NS" "$IPTABLES" -w -t raw -I PREROUTING -i "${CAPTURE_IFACE}1" -m length ! --length 0:65535 -m comment --comment "bigtcp"
	ip netns exec "$CLIENT_NS" "$IPTABLES" -w -t raw -I OUTPUT -o "${CAPTURE_IFACE}0" -m length ! --length 0:65535 -m comment --comment "bigtcp"
	ip netns exec "$SERVER_NS" "$IPTABLES_SACK" -w -t raw -I OUTPUT -o "tun1" -p tcp -m tcp --tcp-flags ACK ACK --tcp-option 5 -m comment --comment "sack"

	if [ "$2" = 4 ]; then
		SERVER_IP="$SERVER_IP4_TUN"
		echo "Running IPv4 traffic in the tunnel"
	else
		SERVER_IP="$SERVER_IP6_TUN"
		echo "Running IPv6 traffic in the tunnel"
	fi

	ip netns exec "$CLIENT_NS" netperf -t TCP_STREAM -l 5 -H "$SERVER_IP" -- \
		-m 80000 > /dev/null

	PACKETS_SERVER=$(ip netns exec "$SERVER_NS" "$IPTABLES-save" -c -t raw | sed -rn '/ --comment bigtcp/{s/^\[([0-9]+):.*/\1/p;q}')
	PACKETS_CLIENT=$(ip netns exec "$CLIENT_NS" "$IPTABLES-save" -c -t raw | sed -rn '/ --comment bigtcp/{s/^\[([0-9]+):.*/\1/p;q}')
	PACKETS_SACK=$(ip netns exec "$SERVER_NS" "$IPTABLES_SACK-save" -c -t raw | sed -rn '/ --comment sack/{s/^\[([0-9]+):.*/\1/p;q}')
	ip netns exec "$SERVER_NS" "$IPTABLES" -w -t raw -D PREROUTING -i "${CAPTURE_IFACE}1" -m length ! --length 0:65535 -m comment --comment "bigtcp"
	ip netns exec "$CLIENT_NS" "$IPTABLES" -w -t raw -D OUTPUT -o "${CAPTURE_IFACE}0" -m length ! --length 0:65535 -m comment --comment "bigtcp"
	ip netns exec "$SERVER_NS" "$IPTABLES_SACK" -w -t raw -D OUTPUT -o "tun1" -p tcp -m tcp --tcp-flags ACK ACK --tcp-option 5 -m comment --comment "sack"

	echo "Captured BIG TCP RX packets: $PACKETS_SERVER"
	echo "Captured BIG TCP TX packets: $PACKETS_CLIENT"
	echo "Captured TCP SACK packets: $PACKETS_SACK"
	[ "$PACKETS_SERVER" -gt "$PACKETS_THRESHOLD" ] || return 1
	[ "$PACKETS_CLIENT" -gt "$PACKETS_THRESHOLD" ] || return 1
	[ "$PACKETS_SACK" -lt "$(( PACKETS_CLIENT / 2 ))" ] || return 1
}

if ! netperf -V &> /dev/null; then
	echo "SKIP: Could not run test without netperf tool"
	exit "$ksft_skip"
fi

if ! iptables --version &> /dev/null; then
	echo "SKIP: Could not run test without iptables tool"
	exit "$ksft_skip"
fi

if ! ethtool --version &> /dev/null; then
	echo "SKIP: Could not run test without ethtool tool"
	exit "$ksft_skip"
fi

if ! ip link help 2>&1 | grep gso_ipv4_max_size &> /dev/null; then
	echo "SKIP: Could not run test without gso/gro_ipv4_max_size supported in ip-link"
	exit "$ksft_skip"
fi

WORKDIR=$(mktemp -d)
trap cleanup EXIT
setup
for tunnel in vxlan geneve; do
	for tun_family in 4 6; do
		for traffic_family in 4 6; do
			for csum_offload in on off; do
				setup_tunnel "$tunnel" "$tun_family" "$csum_offload" || exit "$?"
				do_test "$tun_family" "$traffic_family" "$csum_offload" || exit "$?"
				cleanup_tunnel
			done
		done
	done
done

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Ensures broadcast route MTU is respected

CLIENT_NS=$(mktemp -u client-XXXXXXXX)
CLIENT_IP4="192.168.0.1/24"
CLIENT_BROADCAST_ADDRESS="192.168.0.255"

SERVER_NS=$(mktemp -u server-XXXXXXXX)
SERVER_IP4="192.168.0.2/24"

setup() {
	ip netns add "${CLIENT_NS}"
	ip netns add "${SERVER_NS}"

	ip -net "${SERVER_NS}" link add link1 type veth peer name link0 netns "${CLIENT_NS}"

	ip -net "${CLIENT_NS}" link set link0 up
	ip -net "${CLIENT_NS}" link set link0 mtu 9000
	ip -net "${CLIENT_NS}" addr add "${CLIENT_IP4}" dev link0

	ip -net "${SERVER_NS}" link set link1 up
	ip -net "${SERVER_NS}" link set link1 mtu 1500
	ip -net "${SERVER_NS}" addr add "${SERVER_IP4}" dev link1

	read -r -a CLIENT_BROADCAST_ENTRY <<< "$(ip -net "${CLIENT_NS}" route show table local type broadcast)"
	ip -net "${CLIENT_NS}" route del "${CLIENT_BROADCAST_ENTRY[@]}"
	ip -net "${CLIENT_NS}" route add "${CLIENT_BROADCAST_ENTRY[@]}" mtu 1500

	ip net exec "${SERVER_NS}" sysctl -wq net.ipv4.icmp_echo_ignore_broadcasts=0
}

cleanup() {
	ip -net "${SERVER_NS}" link del link1
	ip netns del "${CLIENT_NS}"
	ip netns del "${SERVER_NS}"
}

trap cleanup EXIT

setup &&
	echo "Testing for broadcast route MTU" &&
	ip net exec "${CLIENT_NS}" ping -f -M want -q -c 1 -s 8000 -w 1 -b "${CLIENT_BROADCAST_ADDRESS}" > /dev/null 2>&1

exit $?


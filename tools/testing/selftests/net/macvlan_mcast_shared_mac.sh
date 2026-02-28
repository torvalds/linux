#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test multicast delivery to macvlan bridge ports when the source MAC
# matches the macvlan's own MAC address (e.g., VRRP virtual MAC shared
# across multiple hosts).
#
# Topology:
#
#   NS_SRC                          NS_BRIDGE
#   veth_src (SHARED_MAC) <----->  veth_dst
#                                     |
#                                     +-- macvlan0 (bridge mode, SHARED_MAC)
#
# A multicast packet sent from NS_SRC with source MAC equal to
# macvlan0's MAC must still be delivered to macvlan0.

source lib.sh

SHARED_MAC="00:00:5e:00:01:01"
MCAST_ADDR="239.0.0.1"

setup() {
	setup_ns NS_SRC NS_BRIDGE

	ip -net "${NS_BRIDGE}" link add veth_dst type veth \
		peer name veth_src netns "${NS_SRC}"

	ip -net "${NS_SRC}" link set veth_src address "${SHARED_MAC}"
	ip -net "${NS_SRC}" link set veth_src up
	ip -net "${NS_SRC}" addr add 192.168.1.1/24 dev veth_src

	ip -net "${NS_BRIDGE}" link set veth_dst up

	ip -net "${NS_BRIDGE}" link add macvlan0 link veth_dst \
		type macvlan mode bridge
	ip -net "${NS_BRIDGE}" link set macvlan0 address "${SHARED_MAC}"
	ip -net "${NS_BRIDGE}" link set macvlan0 up
	ip -net "${NS_BRIDGE}" addr add 192.168.1.2/24 dev macvlan0

	# Accept all multicast so the mc_filter passes for any group.
	ip -net "${NS_BRIDGE}" link set macvlan0 allmulticast on
}

cleanup() {
	rm -f "${CAPFILE}" "${CAPOUT}"
	cleanup_ns "${NS_SRC}" "${NS_BRIDGE}"
}

test_macvlan_mcast_shared_mac() {
	CAPFILE=$(mktemp)
	CAPOUT=$(mktemp)

	echo "Testing multicast delivery to macvlan with shared source MAC"

	# Listen for one ICMP packet on macvlan0.
	timeout 5s ip netns exec "${NS_BRIDGE}" \
		tcpdump -i macvlan0 -c 1 -w "${CAPFILE}" icmp &> "${CAPOUT}" &
	local pid=$!
	if ! slowwait 1 grep -qs "listening" "${CAPOUT}"; then
		echo "[FAIL] tcpdump did not start listening"
		return "${ksft_fail}"
	fi

	# Send multicast ping from NS_SRC; source MAC equals macvlan0's MAC.
	ip netns exec "${NS_SRC}" \
		ping -W 0.1 -c 3 -I veth_src "${MCAST_ADDR}" &> /dev/null

	wait "${pid}"

	local count
	count=$(tcpdump -r "${CAPFILE}" 2>/dev/null | wc -l)
	if [[ "${count}" -ge 1 ]]; then
		echo "[ OK ]"
		return "${ksft_pass}"
	else
		echo "[FAIL] expected at least 1 ICMP packet on macvlan0," \
			"got ${count}"
		return "${ksft_fail}"
	fi
}

if [ ! -x "$(command -v tcpdump)" ]; then
	echo "SKIP: Could not run test without tcpdump tool"
	exit "${ksft_skip}"
fi

trap cleanup EXIT

setup
test_macvlan_mcast_shared_mac

exit $?

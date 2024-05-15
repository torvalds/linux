#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

setup_veth_ns() {
	local -r link_dev="$1"
	local -r ns_name="$2"
	local -r ns_dev="$3"
	local -r ns_mac="$4"

	[[ -e /var/run/netns/"${ns_name}" ]] || ip netns add "${ns_name}"
	echo 1000000 > "/sys/class/net/${ns_dev}/gro_flush_timeout"
	ip link set dev "${ns_dev}" netns "${ns_name}" mtu 65535
	ip -netns "${ns_name}" link set dev "${ns_dev}" up

	ip netns exec "${ns_name}" ethtool -K "${ns_dev}" gro on tso off
}

setup_ns() {
	# Set up server_ns namespace and client_ns namespace
	ip link add name server type veth peer name client

	setup_veth_ns "${dev}" server_ns server "${SERVER_MAC}"
	setup_veth_ns "${dev}" client_ns client "${CLIENT_MAC}"
}

cleanup_ns() {
	local ns_name

	for ns_name in client_ns server_ns; do
		[[ -e /var/run/netns/"${ns_name}" ]] && ip netns del "${ns_name}"
	done
}

setup() {
	# no global init setup step needed
	:
}

cleanup() {
	cleanup_ns
}

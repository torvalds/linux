#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# shellcheck disable=SC2153

source ../lib.sh

IP_SERVER="192.168.200.1"
IP_CLIENT="192.168.200.2"

ppp_common_init() {
	# Package requirements
	require_command socat
	require_command pppd
	require_command iperf3

	# Check for root privileges
	if [ "$(id -u)" -ne 0 ];then
		echo "SKIP: Need root privileges"
		exit "$ksft_skip"
	fi

	# Namespaces
	setup_ns NS_SERVER NS_CLIENT
}

ppp_check_addr() {
	dev=$1
	addr=$2
	ns=$3
	ip -netns "$ns" -4 addr show dev "$dev" 2>/dev/null | grep -q "$addr"
	return $?
}

ppp_test_connectivity() {
	slowwait 10 ppp_check_addr "ppp0" "$IP_CLIENT" "$NS_CLIENT"

	ip netns exec "$NS_CLIENT" ping -c 3 "$IP_SERVER"
	check_err $?

	ip netns exec "$NS_SERVER" iperf3 -s -1 -D
	wait_local_port_listen "$NS_SERVER" 5201 tcp

	ip netns exec "$NS_CLIENT" iperf3 -c "$IP_SERVER" -Z -t 2
	check_err $?
}

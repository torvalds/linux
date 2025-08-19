#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test ensures directed broadcast routes use dst hint mechanism

source lib.sh

CLIENT_IP4="192.168.0.1"
SERVER_IP4="192.168.0.2"
BROADCAST_ADDRESS="192.168.0.255"

setup() {
	setup_ns CLIENT_NS SERVER_NS

	ip -net "${SERVER_NS}" link add link1 type veth peer name link0 netns "${CLIENT_NS}"

	ip -net "${CLIENT_NS}" link set link0 up
	ip -net "${CLIENT_NS}" addr add "${CLIENT_IP4}/24" dev link0

	ip -net "${SERVER_NS}" link set link1 up
	ip -net "${SERVER_NS}" addr add "${SERVER_IP4}/24" dev link1

	ip netns exec "${CLIENT_NS}" ethtool -K link0 tcp-segmentation-offload off
	ip netns exec "${SERVER_NS}" sh -c "echo 500000000 > /sys/class/net/link1/gro_flush_timeout"
	ip netns exec "${SERVER_NS}" sh -c "echo 1 > /sys/class/net/link1/napi_defer_hard_irqs"
	ip netns exec "${SERVER_NS}" ethtool -K link1 generic-receive-offload on
}

cleanup() {
	ip -net "${SERVER_NS}" link del link1
	cleanup_ns "${CLIENT_NS}" "${SERVER_NS}"
}

directed_bcast_hint_test()
{
	local rc=0

	echo "Testing for directed broadcast route hint"

	orig_in_brd=$(ip netns exec "${SERVER_NS}" lnstat -j -i1 -c1 | jq '.in_brd')
	ip netns exec "${CLIENT_NS}" mausezahn link0 -a own -b bcast -A "${CLIENT_IP4}" \
		-B "${BROADCAST_ADDRESS}" -c1 -t tcp "sp=1-100,dp=1234,s=1,a=0" -p 5 -q
	sleep 1
	new_in_brd=$(ip netns exec "${SERVER_NS}" lnstat -j -i1 -c1 | jq '.in_brd')

	res=$(echo "${new_in_brd} - ${orig_in_brd}" | bc)

	if [ "${res}" -lt 100 ]; then
		echo "[ OK ]"
		rc="${ksft_pass}"
	else
		echo "[FAIL] expected in_brd to be under 100, got ${res}"
		rc="${ksft_fail}"
	fi

	return "${rc}"
}

if [ ! -x "$(command -v mausezahn)" ]; then
	echo "SKIP: Could not run test without mausezahn tool"
	exit "${ksft_skip}"
fi

if [ ! -x "$(command -v jq)" ]; then
	echo "SKIP: Could not run test without jq tool"
	exit "${ksft_skip}"
fi

if [ ! -x "$(command -v bc)" ]; then
	echo "SKIP: Could not run test without bc tool"
	exit "${ksft_skip}"
fi

trap cleanup EXIT

setup

directed_bcast_hint_test
exit $?

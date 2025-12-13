#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Author: Brett A C Sheffield <bacs@librecast.net>
# Author: Oscar Maes <oscmaes92@gmail.com>
#
# Ensure destination ethernet field is correctly set for
# broadcast packets

source lib.sh

CLIENT_IP4="192.168.0.1"
GW_IP4="192.168.0.2"

setup() {
	setup_ns CLIENT_NS SERVER_NS

	ip -net "${SERVER_NS}" link add link1 type veth \
		peer name link0 netns "${CLIENT_NS}"

	ip -net "${CLIENT_NS}" link set link0 up
	ip -net "${CLIENT_NS}" addr add "${CLIENT_IP4}"/24 dev link0

	ip -net "${SERVER_NS}" link set link1 up

	ip -net "${CLIENT_NS}" route add default via "${GW_IP4}"
	ip netns exec "${CLIENT_NS}" arp -s "${GW_IP4}" 00:11:22:33:44:55
}

cleanup() {
	rm -f "${CAPFILE}" "${OUTPUT}"
	ip -net "${SERVER_NS}" link del link1
	cleanup_ns "${CLIENT_NS}" "${SERVER_NS}"
}

test_broadcast_ether_dst() {
	local rc=0
	CAPFILE=$(mktemp -u cap.XXXXXXXXXX)
	OUTPUT=$(mktemp -u out.XXXXXXXXXX)

	echo "Testing ethernet broadcast destination"

	# start tcpdump listening for icmp
	# tcpdump will exit after receiving a single packet
	# timeout will kill tcpdump if it is still running after 2s
	timeout 2s ip netns exec "${CLIENT_NS}" \
		tcpdump -i link0 -c 1 -w "${CAPFILE}" icmp &> "${OUTPUT}" &
	pid=$!
	slowwait 1 grep -qs "listening" "${OUTPUT}"

	# send broadcast ping
	ip netns exec "${CLIENT_NS}" \
		ping -W0.01 -c1 -b 255.255.255.255 &> /dev/null

	# wait for tcpdump for exit after receiving packet
	wait "${pid}"

	# compare ethernet destination field to ff:ff:ff:ff:ff:ff
	ether_dst=$(tcpdump -r "${CAPFILE}" -tnne 2>/dev/null | \
			awk '{sub(/,/,"",$3); print $3}')
	if [[ "${ether_dst}" == "ff:ff:ff:ff:ff:ff" ]]; then
		echo "[ OK ]"
		rc="${ksft_pass}"
	else
		echo "[FAIL] expected dst ether addr to be ff:ff:ff:ff:ff:ff," \
			"got ${ether_dst}"
		rc="${ksft_fail}"
	fi

	return "${rc}"
}

if [ ! -x "$(command -v tcpdump)" ]; then
	echo "SKIP: Could not run test without tcpdump tool"
	exit "${ksft_skip}"
fi

trap cleanup EXIT

setup
test_broadcast_ether_dst

exit $?

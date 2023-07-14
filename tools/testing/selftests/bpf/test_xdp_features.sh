#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

readonly NS="ns1-$(mktemp -u XXXXXX)"
readonly V0_IP4=10.10.0.11
readonly V1_IP4=10.10.0.1
readonly V0_IP6=2001:db8::11
readonly V1_IP6=2001:db8::1

ret=1

setup() {
	{
		ip netns add ${NS}

		ip link add v1 type veth peer name v0 netns ${NS}

		ip link set v1 up
		ip addr add $V1_IP4/24 dev v1
		ip addr add $V1_IP6/64 nodad dev v1
		ip -n ${NS} link set dev v0 up
		ip -n ${NS} addr add $V0_IP4/24 dev v0
		ip -n ${NS} addr add $V0_IP6/64 nodad dev v0

		# Enable XDP mode and disable checksum offload
		ethtool -K v1 gro on
		ethtool -K v1 tx-checksumming off
		ip netns exec ${NS} ethtool -K v0 gro on
		ip netns exec ${NS} ethtool -K v0 tx-checksumming off
	} > /dev/null 2>&1
}

cleanup() {
	ip link del v1 2> /dev/null
	ip netns del ${NS} 2> /dev/null
	[ "$(pidof xdp_features)" = "" ] || kill $(pidof xdp_features) 2> /dev/null
}

wait_for_dut_server() {
	while sleep 1; do
		ss -tlp | grep -q xdp_features
		[ $? -eq 0 ] && break
	done
}

test_xdp_features() {
	setup

	## XDP_PASS
	./xdp_features -f XDP_PASS -D $V1_IP6 -T $V0_IP6 v1 &
	wait_for_dut_server
	ip netns exec ${NS} ./xdp_features -t -f XDP_PASS \
					   -D $V1_IP6 -C $V1_IP6 \
					   -T $V0_IP6 v0
	[ $? -ne 0 ] && exit

	## XDP_DROP
	./xdp_features -f XDP_DROP -D ::ffff:$V1_IP4 -T ::ffff:$V0_IP4 v1 &
	wait_for_dut_server
	ip netns exec ${NS} ./xdp_features -t -f XDP_DROP \
					   -D ::ffff:$V1_IP4 \
					   -C ::ffff:$V1_IP4 \
					   -T ::ffff:$V0_IP4 v0
	[ $? -ne 0 ] && exit

	## XDP_ABORTED
	./xdp_features -f XDP_ABORTED -D $V1_IP6 -T $V0_IP6 v1 &
	wait_for_dut_server
	ip netns exec ${NS} ./xdp_features -t -f XDP_ABORTED \
					   -D $V1_IP6 -C $V1_IP6 \
					   -T $V0_IP6 v0
	[ $? -ne 0 ] && exit

	## XDP_TX
	./xdp_features -f XDP_TX -D ::ffff:$V1_IP4 -T ::ffff:$V0_IP4 v1 &
	wait_for_dut_server
	ip netns exec ${NS} ./xdp_features -t -f XDP_TX \
					   -D ::ffff:$V1_IP4 \
					   -C ::ffff:$V1_IP4 \
					   -T ::ffff:$V0_IP4 v0
	[ $? -ne 0 ] && exit

	## XDP_REDIRECT
	./xdp_features -f XDP_REDIRECT -D $V1_IP6 -T $V0_IP6 v1 &
	wait_for_dut_server
	ip netns exec ${NS} ./xdp_features -t -f XDP_REDIRECT \
					   -D $V1_IP6 -C $V1_IP6 \
					   -T $V0_IP6 v0
	[ $? -ne 0 ] && exit

	## XDP_NDO_XMIT
	./xdp_features -f XDP_NDO_XMIT -D ::ffff:$V1_IP4 -T ::ffff:$V0_IP4 v1 &
	wait_for_dut_server
	ip netns exec ${NS} ./xdp_features -t -f XDP_NDO_XMIT \
					   -D ::ffff:$V1_IP4 \
					   -C ::ffff:$V1_IP4 \
					   -T ::ffff:$V0_IP4 v0
	ret=$?
	cleanup
}

set -e
trap cleanup 2 3 6 9

test_xdp_features

exit $ret

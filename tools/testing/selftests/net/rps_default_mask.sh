#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

readonly ksft_skip=4
readonly cpus=$(nproc)
ret=0

[ $cpus -gt 2 ] || exit $ksft_skip

readonly INITIAL_RPS_DEFAULT_MASK=$(cat /proc/sys/net/core/rps_default_mask)
readonly TAG="$(mktemp -u XXXXXX)"
readonly VETH="veth${TAG}"
readonly NETNS="ns-${TAG}"

setup() {
	ip netns add "${NETNS}"
	ip -netns "${NETNS}" link set lo up
}

cleanup() {
	echo $INITIAL_RPS_DEFAULT_MASK > /proc/sys/net/core/rps_default_mask
	ip netns del $NETNS
}

chk_rps() {
	local rps_mask expected_rps_mask=$4
	local dev_name=$3
	local netns=$2
	local cmd="cat"
	local msg=$1

	[ -n "$netns" ] && cmd="ip netns exec $netns $cmd"

	rps_mask=$($cmd /sys/class/net/$dev_name/queues/rx-0/rps_cpus)
	printf "%-60s" "$msg"
	if [ $rps_mask -eq $expected_rps_mask ]; then
		echo "[ ok ]"
	else
		echo "[fail] expected $expected_rps_mask found $rps_mask"
		ret=1
	fi
}

trap cleanup EXIT

echo 0 > /proc/sys/net/core/rps_default_mask
setup
chk_rps "empty rps_default_mask" $NETNS lo 0
cleanup

echo 1 > /proc/sys/net/core/rps_default_mask
setup
chk_rps "changing rps_default_mask dont affect existing devices" "" lo $INITIAL_RPS_DEFAULT_MASK

echo 3 > /proc/sys/net/core/rps_default_mask
chk_rps "changing rps_default_mask dont affect existing netns" $NETNS lo 0

ip link add name $VETH type veth peer netns $NETNS name $VETH
ip link set dev $VETH up
ip -n $NETNS link set dev $VETH up
chk_rps "changing rps_default_mask affect newly created devices" "" $VETH 3
chk_rps "changing rps_default_mask don't affect newly child netns[II]" $NETNS $VETH 0
ip netns del $NETNS

setup
chk_rps "rps_default_mask is 0 by default in child netns" "$NETNS" lo 0

ip netns exec $NETNS sysctl -qw net.core.rps_default_mask=1
ip link add name $VETH type veth peer netns $NETNS name $VETH
chk_rps "changing rps_default_mask in child ns don't affect the main one" "" lo $INITIAL_RPS_DEFAULT_MASK
chk_rps "changing rps_default_mask in child ns affects new childns devices" $NETNS $VETH 1
chk_rps "changing rps_default_mask in child ns don't affect existing devices" $NETNS lo 0

exit $ret

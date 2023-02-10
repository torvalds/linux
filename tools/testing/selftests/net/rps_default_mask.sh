#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

readonly ksft_skip=4
readonly cpus=$(nproc)
ret=0

[ $cpus -gt 2 ] || exit $ksft_skip

readonly INITIAL_RPS_DEFAULT_MASK=$(cat /proc/sys/net/core/rps_default_mask)
readonly NETNS="ns-$(mktemp -u XXXXXX)"

setup() {
	ip netns add "${NETNS}"
	ip -netns "${NETNS}" link set lo up
}

cleanup() {
	echo $INITIAL_RPS_DEFAULT_MASK > /proc/sys/net/core/rps_default_mask
	ip netns del $NETNS
}

chk_rps() {
	local rps_mask expected_rps_mask=$3
	local dev_name=$2
	local msg=$1

	rps_mask=$(ip netns exec $NETNS cat /sys/class/net/$dev_name/queues/rx-0/rps_cpus)
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
chk_rps "empty rps_default_mask" lo 0
cleanup

echo 1 > /proc/sys/net/core/rps_default_mask
setup
chk_rps "non zero rps_default_mask" lo 1

echo 3 > /proc/sys/net/core/rps_default_mask
chk_rps "changing rps_default_mask dont affect existing netns" lo 1

ip -n $NETNS link add type veth
ip -n $NETNS link set dev veth0 up
ip -n $NETNS link set dev veth1 up
chk_rps "changing rps_default_mask affect newly created devices" veth0 3
chk_rps "changing rps_default_mask affect newly created devices[II]" veth1 3
exit $ret

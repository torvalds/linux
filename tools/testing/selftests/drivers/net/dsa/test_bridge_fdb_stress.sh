#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Bridge FDB entries can be offloaded to DSA switches without holding the
# rtnl_mutex. Traditionally this mutex has conferred drivers implicit
# serialization, which means their code paths are not well tested in the
# presence of concurrency.
# This test creates a background task that stresses the FDB by adding and
# deleting an entry many times in a row without the rtnl_mutex held.
# It then tests the driver resistance to concurrency by calling .ndo_fdb_dump
# (with rtnl_mutex held) from a foreground task.
# Since either the FDB dump or the additions/removals can fail, but the
# additions and removals are performed in deferred as opposed to process
# context, we cannot simply check for user space error codes.

WAIT_TIME=1
NUM_NETIFS=1
REQUIRE_JQ="no"
REQUIRE_MZ="no"
NETIF_CREATE="no"
lib_dir=$(dirname "$0")
source "$lib_dir"/lib.sh

cleanup() {
	echo "Cleaning up"
	kill $pid && wait $pid &> /dev/null
	ip link del br0
	echo "Please check kernel log for errors"
}
trap 'cleanup' EXIT

eth=${NETIFS[p1]}

ip link del br0 2&>1 >/dev/null || :
ip link add br0 type bridge && ip link set $eth master br0

(while :; do
	bridge fdb add 00:01:02:03:04:05 dev $eth master static
	bridge fdb del 00:01:02:03:04:05 dev $eth master static
done) &
pid=$!

for i in $(seq 1 50); do
	bridge fdb show > /dev/null
	sleep 3
	echo "$((${i} * 2))% complete..."
done

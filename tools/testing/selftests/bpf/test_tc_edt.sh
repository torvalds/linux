#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test installs a TC bpf program that throttles a TCP flow
# with dst port = 9000 down to 5MBps. Then it measures actual
# throughput of the flow.

if [[ $EUID -ne 0 ]]; then
	echo "This script must be run as root"
	echo "FAIL"
	exit 1
fi

# check that nc, dd, and timeout are present
command -v nc >/dev/null 2>&1 || \
	{ echo >&2 "nc is not available"; exit 1; }
command -v dd >/dev/null 2>&1 || \
	{ echo >&2 "nc is not available"; exit 1; }
command -v timeout >/dev/null 2>&1 || \
	{ echo >&2 "timeout is not available"; exit 1; }

readonly NS_SRC="ns-src-$(mktemp -u XXXXXX)"
readonly NS_DST="ns-dst-$(mktemp -u XXXXXX)"

readonly IP_SRC="172.16.1.100"
readonly IP_DST="172.16.2.100"

cleanup()
{
	ip netns del ${NS_SRC}
	ip netns del ${NS_DST}
}

trap cleanup EXIT

set -e  # exit on error

ip netns add "${NS_SRC}"
ip netns add "${NS_DST}"
ip link add veth_src type veth peer name veth_dst
ip link set veth_src netns ${NS_SRC}
ip link set veth_dst netns ${NS_DST}

ip -netns ${NS_SRC} addr add ${IP_SRC}/24  dev veth_src
ip -netns ${NS_DST} addr add ${IP_DST}/24  dev veth_dst

ip -netns ${NS_SRC} link set dev veth_src up
ip -netns ${NS_DST} link set dev veth_dst up

ip -netns ${NS_SRC} route add ${IP_DST}/32  dev veth_src
ip -netns ${NS_DST} route add ${IP_SRC}/32  dev veth_dst

# set up TC on TX
ip netns exec ${NS_SRC} tc qdisc add dev veth_src root fq
ip netns exec ${NS_SRC} tc qdisc add dev veth_src clsact
ip netns exec ${NS_SRC} tc filter add dev veth_src egress \
	bpf da obj test_tc_edt.o sec cls_test


# start the listener
ip netns exec ${NS_DST} bash -c \
	"nc -4 -l -s ${IP_DST} -p 9000 >/dev/null &"
declare -i NC_PID=$!
sleep 1

declare -ir TIMEOUT=20
declare -ir EXPECTED_BPS=5000000

# run the load, capture RX bytes on DST
declare -ir RX_BYTES_START=$( ip netns exec ${NS_DST} \
	cat /sys/class/net/veth_dst/statistics/rx_bytes )

set +e
ip netns exec ${NS_SRC} bash -c "timeout ${TIMEOUT} dd if=/dev/zero \
	bs=1000 count=1000000 > /dev/tcp/${IP_DST}/9000 2>/dev/null"
set -e

declare -ir RX_BYTES_END=$( ip netns exec ${NS_DST} \
	cat /sys/class/net/veth_dst/statistics/rx_bytes )

declare -ir ACTUAL_BPS=$(( ($RX_BYTES_END - $RX_BYTES_START) / $TIMEOUT ))

echo $TIMEOUT $ACTUAL_BPS $EXPECTED_BPS | \
	awk '{printf "elapsed: %d sec; bps difference: %.2f%%\n",
		$1, ($2-$3)*100.0/$3}'

# Pass the test if the actual bps is within 1% of the expected bps.
# The difference is usually about 0.1% on a 20-sec test, and ==> zero
# the longer the test runs.
declare -ir RES=$( echo $ACTUAL_BPS $EXPECTED_BPS | \
	 awk 'function abs(x){return ((x < 0.0) ? -x : x)}
	      {if (abs(($1-$2)*100.0/$2) > 1.0) { print "1" }
		else { print "0"} }' )
if [ "${RES}" == "0" ] ; then
	echo "PASS"
else
	echo "FAIL"
	exit 1
fi

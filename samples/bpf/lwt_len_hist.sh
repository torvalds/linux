#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NS1=lwt_ns1
VETH0=tst_lwt1a
VETH1=tst_lwt1b

TRACE_ROOT=/sys/kernel/debug/tracing

function cleanup {
	# To reset saved histogram, remove pinned map
	rm /sys/fs/bpf/tc/globals/lwt_len_hist_map
	ip route del 192.168.253.2/32 dev $VETH0 2> /dev/null
	ip link del $VETH0 2> /dev/null
	ip link del $VETH1 2> /dev/null
	ip netns exec $NS1 killall netserver
	ip netns delete $NS1 2> /dev/null
}

cleanup

ip netns add $NS1
ip link add $VETH0 type veth peer name $VETH1
ip link set dev $VETH0 up
ip addr add 192.168.253.1/24 dev $VETH0
ip link set $VETH1 netns $NS1
ip netns exec $NS1 ip link set dev $VETH1 up
ip netns exec $NS1 ip addr add 192.168.253.2/24 dev $VETH1
ip netns exec $NS1 netserver

echo 1 > ${TRACE_ROOT}/tracing_on
cp /dev/null ${TRACE_ROOT}/trace
ip route add 192.168.253.2/32 encap bpf out obj lwt_len_hist_kern.o section len_hist dev $VETH0
netperf -H 192.168.253.2 -t TCP_STREAM
cat ${TRACE_ROOT}/trace | grep -v '^#'
./lwt_len_hist
cleanup
echo 0 > ${TRACE_ROOT}/tracing_on

exit 0

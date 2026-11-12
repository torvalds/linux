#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Check software GSO of NSH packets: a TCP GSO packet behind a few NSH
# headers is segmented, and a deep stack of NSH headers is dropped
# instead of recursing in nsh_gso_segment() for each of them.

source lib.sh

NR_SEGS=4
ret=0

cleanup()
{
	cleanup_ns $ns1
}

tx_packets()
{
	ip netns exec $ns1 cat /sys/class/net/dummy0/statistics/tx_packets
}

# $1: number of NSH headers, $2: expected number of transmitted packets
check_nsh_gso()
{
	local depth=$1
	local expected=$2
	local before
	local sent

	before=$(tx_packets)
	ip netns exec $ns1 ./nsh_gso dummy0 $depth
	sent=$(($(tx_packets) - before))

	if [ $sent -eq $expected ]; then
		echo "PASS: $depth NSH headers: $sent packets sent"
	else
		echo "FAIL: $depth NSH headers: $sent packets sent, expected $expected"
		ret=1
	fi
}

modprobe -q nsh
nsh_loaded=$?

trap cleanup EXIT
setup_ns ns1 || exit $ksft_skip

ip -netns $ns1 link add dummy0 type dummy
ip -netns $ns1 link set dummy0 up
# No IPv6 autoconf traffic on the device, to get stable counters.
ip netns exec $ns1 sysctl -qw net.ipv6.conf.dummy0.disable_ipv6=1
# Force software segmentation in validate_xmit_skb().
ip netns exec $ns1 ethtool -K dummy0 tso off >/dev/null || exit $ksft_skip

before=$(tx_packets)
ip netns exec $ns1 ./nsh_gso dummy0 1
if [ $(($(tx_packets) - before)) -eq 0 ] && [ $nsh_loaded -ne 0 ]; then
	echo "SKIP: NSH GSO not available"
	exit $ksft_skip
fi

check_nsh_gso 1 $NR_SEGS
# The recursion limit is shared with the inner IPv4 header.
check_nsh_gso 4 $NR_SEGS
check_nsh_gso 5 0
check_nsh_gso 64 0
# The largest stack that fits in a 64KB header.
check_nsh_gso 8000 0

exit $ret

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Create 3 namespaces with 3 veth peers, and
# forward packets in-between using native XDP
#
#                      XDP_TX
# NS1(veth11)        NS2(veth22)        NS3(veth33)
#      |                  |                  |
#      |                  |                  |
#   (veth1,            (veth2,            (veth3,
#   id:111)            id:122)            id:133)
#     ^ |                ^ |                ^ |
#     | |  XDP_REDIRECT  | |  XDP_REDIRECT  | |
#     | ------------------ ------------------ |
#     -----------------------------------------
#                    XDP_REDIRECT

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

TESTNAME=xdp_veth
BPF_FS=$(awk '$3 == "bpf" {print $2; exit}' /proc/mounts)
BPF_DIR=$BPF_FS/test_$TESTNAME
readonly NS1="ns1-$(mktemp -u XXXXXX)"
readonly NS2="ns2-$(mktemp -u XXXXXX)"
readonly NS3="ns3-$(mktemp -u XXXXXX)"

_cleanup()
{
	set +e
	ip link del veth1 2> /dev/null
	ip link del veth2 2> /dev/null
	ip link del veth3 2> /dev/null
	ip netns del ${NS1} 2> /dev/null
	ip netns del ${NS2} 2> /dev/null
	ip netns del ${NS3} 2> /dev/null
	rm -rf $BPF_DIR 2> /dev/null
}

cleanup_skip()
{
	echo "selftests: $TESTNAME [SKIP]"
	_cleanup

	exit $ksft_skip
}

cleanup()
{
	if [ "$?" = 0 ]; then
		echo "selftests: $TESTNAME [PASS]"
	else
		echo "selftests: $TESTNAME [FAILED]"
	fi
	_cleanup
}

if [ $(id -u) -ne 0 ]; then
	echo "selftests: $TESTNAME [SKIP] Need root privileges"
	exit $ksft_skip
fi

if ! ip link set dev lo xdp off > /dev/null 2>&1; then
	echo "selftests: $TESTNAME [SKIP] Could not run test without the ip xdp support"
	exit $ksft_skip
fi

if [ -z "$BPF_FS" ]; then
	echo "selftests: $TESTNAME [SKIP] Could not run test without bpffs mounted"
	exit $ksft_skip
fi

if ! bpftool version > /dev/null 2>&1; then
	echo "selftests: $TESTNAME [SKIP] Could not run test without bpftool"
	exit $ksft_skip
fi

set -e

trap cleanup_skip EXIT

ip netns add ${NS1}
ip netns add ${NS2}
ip netns add ${NS3}

ip link add veth1 index 111 type veth peer name veth11 netns ${NS1}
ip link add veth2 index 122 type veth peer name veth22 netns ${NS2}
ip link add veth3 index 133 type veth peer name veth33 netns ${NS3}

ip link set veth1 up
ip link set veth2 up
ip link set veth3 up

ip -n ${NS1} addr add 10.1.1.11/24 dev veth11
ip -n ${NS3} addr add 10.1.1.33/24 dev veth33

ip -n ${NS1} link set dev veth11 up
ip -n ${NS2} link set dev veth22 up
ip -n ${NS3} link set dev veth33 up

mkdir $BPF_DIR
bpftool prog loadall \
	xdp_redirect_map.o $BPF_DIR/progs type xdp \
	pinmaps $BPF_DIR/maps
bpftool map update pinned $BPF_DIR/maps/tx_port key 0 0 0 0 value 122 0 0 0
bpftool map update pinned $BPF_DIR/maps/tx_port key 1 0 0 0 value 133 0 0 0
bpftool map update pinned $BPF_DIR/maps/tx_port key 2 0 0 0 value 111 0 0 0
ip link set dev veth1 xdp pinned $BPF_DIR/progs/xdp_redirect_map_0
ip link set dev veth2 xdp pinned $BPF_DIR/progs/xdp_redirect_map_1
ip link set dev veth3 xdp pinned $BPF_DIR/progs/xdp_redirect_map_2

ip -n ${NS1} link set dev veth11 xdp obj xdp_dummy.o sec xdp
ip -n ${NS2} link set dev veth22 xdp obj xdp_tx.o sec xdp
ip -n ${NS3} link set dev veth33 xdp obj xdp_dummy.o sec xdp

trap cleanup EXIT

ip netns exec ${NS1} ping -c 1 -W 1 10.1.1.33

exit 0

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test runs a simple ingress tc setup between two veth pairs,
# and chains a single egress rule to test ingress chaining to egress.
#
# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if [ "$(id -u)" -ne 0 ];then
	echo "SKIP: Need root privileges"
	exit $ksft_skip
fi

needed_mods="act_mirred cls_flower sch_ingress"
for mod in $needed_mods; do
	modinfo $mod &>/dev/null || { echo "SKIP: Need act_mirred module"; exit $ksft_skip; }
done

ns="ns$((RANDOM%899+100))"
veth1="veth1$((RANDOM%899+100))"
veth2="veth2$((RANDOM%899+100))"
peer1="peer1$((RANDOM%899+100))"
peer2="peer2$((RANDOM%899+100))"
ip_peer1=198.51.100.5
ip_peer2=198.51.100.6

function fail() {
	echo "FAIL: $@" >> /dev/stderr
	exit 1
}

function cleanup() {
	killall -q -9 udpgso_bench_rx
	ip link del $veth1 &> /dev/null
	ip link del $veth2 &> /dev/null
	ip netns del $ns &> /dev/null
}
trap cleanup EXIT

function config() {
	echo "Setup veth pairs [$veth1, $peer1], and veth pair [$veth2, $peer2]"
	ip link add $veth1 type veth peer name $peer1
	ip link add $veth2 type veth peer name $peer2
	ip addr add $ip_peer1/24 dev $peer1
	ip link set $peer1 up
	ip netns add $ns
	ip link set dev $peer2 netns $ns
	ip netns exec $ns ip addr add $ip_peer2/24 dev $peer2
	ip netns exec $ns ip link set $peer2 up
	ip link set $veth1 up
	ip link set $veth2 up

	echo "Add tc filter ingress->egress forwarding $veth1 <-> $veth2"
	tc qdisc add dev $veth2 ingress
	tc qdisc add dev $veth1 ingress
	tc filter add dev $veth2 ingress prio 1 proto all flower \
		action mirred egress redirect dev $veth1
	tc filter add dev $veth1 ingress prio 1 proto all flower \
		action mirred egress redirect dev $veth2

	echo "Add tc filter egress->ingress forwarding $peer1 -> $veth1, bypassing the veth pipe"
	tc qdisc add dev $peer1 clsact
	tc filter add dev $peer1 egress prio 20 proto ip flower \
		action mirred ingress redirect dev $veth1
}

function test_run() {
	echo "Run tcp traffic"
	./udpgso_bench_rx -t &
	sleep 1
	ip netns exec $ns timeout -k 2 10 ./udpgso_bench_tx -t -l 2 -4 -D $ip_peer1 || fail "traffic failed"
	echo "Test passed"
}

config
test_run
trap - EXIT
cleanup

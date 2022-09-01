#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgro benchmarks

readonly PEER_NS="ns-peer-$(mktemp -u XXXXXX)"

cleanup() {
	local -r jobs="$(jobs -p)"
	local -r ns="$(ip netns list|grep $PEER_NS)"

	[ -n "${jobs}" ] && kill -INT ${jobs} 2>/dev/null
	[ -n "$ns" ] && ip netns del $ns 2>/dev/null
}
trap cleanup EXIT

run_one() {
	# use 'rx' as separator between sender args and receiver args
	local -r all="$@"
	local -r tx_args=${all%rx*}
	local rx_args=${all#*rx}

	[[ "${tx_args}" == *"-4"* ]] && rx_args="${rx_args} -4"

	ip netns add "${PEER_NS}"
	ip -netns "${PEER_NS}" link set lo up
	ip link add type veth
	ip link set dev veth0 up
	ip addr add dev veth0 192.168.1.2/24
	ip addr add dev veth0 2001:db8::2/64 nodad

	ip link set dev veth1 netns "${PEER_NS}"
	ip -netns "${PEER_NS}" addr add dev veth1 192.168.1.1/24
	ip -netns "${PEER_NS}" addr add dev veth1 2001:db8::1/64 nodad
	ip -netns "${PEER_NS}" link set dev veth1 up

	ip -n "${PEER_NS}" link set veth1 xdp object ../bpf/xdp_dummy.o section xdp
	ip netns exec "${PEER_NS}" ./udpgso_bench_rx ${rx_args} -r &
	ip netns exec "${PEER_NS}" ./udpgso_bench_rx -t ${rx_args} -r &

	# Hack: let bg programs complete the startup
	sleep 0.1
	./udpgso_bench_tx ${tx_args}
}

run_in_netns() {
	local -r args=$@

	./in_netns.sh $0 __subprocess ${args}
}

run_udp() {
	local -r args=$@

	echo "udp gso - over veth touching data"
	run_in_netns ${args} -S 0 rx

	echo "udp gso and gro - over veth touching data"
	run_in_netns ${args} -S 0 rx -G
}

run_tcp() {
	local -r args=$@

	echo "tcp - over veth touching data"
	run_in_netns ${args} -t rx
}

run_all() {
	local -r core_args="-l 4"
	local -r ipv4_args="${core_args} -4 -D 192.168.1.1"
	local -r ipv6_args="${core_args} -6 -D 2001:db8::1"

	echo "ipv4"
	run_tcp "${ipv4_args}"
	run_udp "${ipv4_args}"

	echo "ipv6"
	run_tcp "${ipv4_args}"
	run_udp "${ipv6_args}"
}

if [ ! -f ../bpf/xdp_dummy.o ]; then
	echo "Missing xdp_dummy helper. Build bpf selftest first"
	exit -1
fi

if [[ $# -eq 0 ]]; then
	run_all
elif [[ $1 == "__subprocess" ]]; then
	shift
	run_one $@
else
	run_in_netns $@
fi

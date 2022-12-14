#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgro benchmarks

readonly PEER_NS="ns-peer-$(mktemp -u XXXXXX)"

BPF_FILE="../bpf/xdp_dummy.bpf.o"

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
	ip netns exec "${PEER_NS}" ethtool -K veth1 rx-gro-list on


	ip -n "${PEER_NS}" link set veth1 xdp object ${BPF_FILE} section xdp
	tc -n "${PEER_NS}" qdisc add dev veth1 clsact
	tc -n "${PEER_NS}" filter add dev veth1 ingress prio 4 protocol ipv6 bpf object-file ../bpf/nat6to4.o section schedcls/ingress6/nat_6  direct-action
	tc -n "${PEER_NS}" filter add dev veth1 egress prio 4 protocol ip bpf object-file ../bpf/nat6to4.o section schedcls/egress4/snat4 direct-action
        echo ${rx_args}
	ip netns exec "${PEER_NS}" ./udpgso_bench_rx ${rx_args} -r &

	# Hack: let bg programs complete the startup
	sleep 0.2
	./udpgso_bench_tx ${tx_args}
}

run_in_netns() {
	local -r args=$@
  echo ${args}
	./in_netns.sh $0 __subprocess ${args}
}

run_udp() {
	local -r args=$@

	echo "udp gso - over veth touching data"
	run_in_netns ${args} -u -S 0 rx -4 -v

	echo "udp gso and gro - over veth touching data"
	run_in_netns ${args} -S 0 rx -4 -G
}

run_tcp() {
	local -r args=$@

	echo "tcp - over veth touching data"
	run_in_netns ${args} -t rx -4 -t
}

run_all() {
	local -r core_args="-l 4"
	local -r ipv4_args="${core_args} -4  -D 192.168.1.1"
	local -r ipv6_args="${core_args} -6  -D 2001:db8::1"

	echo "ipv6"
	run_tcp "${ipv6_args}"
	run_udp "${ipv6_args}"
}

if [ ! -f ${BPF_FILE} ]; then
	echo "Missing ${BPF_FILE}. Build bpf selftest first"
	exit -1
fi

if [ ! -f bpf/nat6to4.o ]; then
	echo "Missing nat6to4 helper. Build bpfnat6to4.o selftest first"
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

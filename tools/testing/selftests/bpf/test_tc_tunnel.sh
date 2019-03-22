#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# In-place tunneling

# must match the port that the bpf program filters on
readonly port=8000

readonly ns_prefix="ns-$$-"
readonly ns1="${ns_prefix}1"
readonly ns2="${ns_prefix}2"

readonly ns1_v4=192.168.1.1
readonly ns2_v4=192.168.1.2
readonly ns1_v6=fd::1
readonly ns2_v6=fd::2

readonly infile="$(mktemp)"
readonly outfile="$(mktemp)"

setup() {
	ip netns add "${ns1}"
	ip netns add "${ns2}"

	ip link add dev veth1 mtu 1500 netns "${ns1}" type veth \
	      peer name veth2 mtu 1500 netns "${ns2}"

	ip netns exec "${ns1}" ethtool -K veth1 tso off

	ip -netns "${ns1}" link set veth1 up
	ip -netns "${ns2}" link set veth2 up

	ip -netns "${ns1}" -4 addr add "${ns1_v4}/24" dev veth1
	ip -netns "${ns2}" -4 addr add "${ns2_v4}/24" dev veth2
	ip -netns "${ns1}" -6 addr add "${ns1_v6}/64" dev veth1 nodad
	ip -netns "${ns2}" -6 addr add "${ns2_v6}/64" dev veth2 nodad

	sleep 1

	dd if=/dev/urandom of="${infile}" bs="${datalen}" count=1 status=none
}

cleanup() {
	ip netns del "${ns2}"
	ip netns del "${ns1}"

	if [[ -f "${outfile}" ]]; then
		rm "${outfile}"
	fi
	if [[ -f "${infile}" ]]; then
		rm "${infile}"
	fi
}

server_listen() {
	ip netns exec "${ns2}" nc "${netcat_opt}" -l -p "${port}" > "${outfile}" &
	server_pid=$!
	sleep 0.2
}

client_connect() {
	ip netns exec "${ns1}" timeout 2 nc "${netcat_opt}" -w 1 "${addr2}" "${port}" < "${infile}"
	echo $?
}

verify_data() {
	wait "${server_pid}"
	# sha1sum returns two fields [sha1] [filepath]
	# convert to bash array and access first elem
	insum=($(sha1sum ${infile}))
	outsum=($(sha1sum ${outfile}))
	if [[ "${insum[0]}" != "${outsum[0]}" ]]; then
		echo "data mismatch"
		exit 1
	fi
}

set -e

# no arguments: automated test, run all
if [[ "$#" -eq "0" ]]; then
	echo "ipip"
	$0 ipv4 ipip 100

	echo "ip6ip6"
	$0 ipv6 ip6tnl 100

	echo "ip gre"
	$0 ipv4 gre 100

	echo "ip6 gre"
	$0 ipv6 ip6gre 100

	# disabled until passes SKB_GSO_DODGY checks
	# echo "ip gre gso"
	# $0 ipv4 gre 2000

	# disabled until passes SKB_GSO_DODGY checks
	# echo "ip6 gre gso"
	# $0 ipv6 ip6gre 2000

	echo "OK. All tests passed"
	exit 0
fi

if [[ "$#" -ne "3" ]]; then
	echo "Usage: $0"
	echo "   or: $0 <ipv4|ipv6> <tuntype> <data_len>"
	exit 1
fi

case "$1" in
"ipv4")
	readonly addr1="${ns1_v4}"
	readonly addr2="${ns2_v4}"
	readonly netcat_opt=-4
	;;
"ipv6")
	readonly addr1="${ns1_v6}"
	readonly addr2="${ns2_v6}"
	readonly netcat_opt=-6
	;;
*)
	echo "unknown arg: $1"
	exit 1
	;;
esac

readonly tuntype=$2
readonly datalen=$3

echo "encap ${addr1} to ${addr2}, type ${tuntype}, len ${datalen}"

trap cleanup EXIT

setup

# basic communication works
echo "test basic connectivity"
server_listen
client_connect
verify_data

# clientside, insert bpf program to encap all TCP to port ${port}
# client can no longer connect
ip netns exec "${ns1}" tc qdisc add dev veth1 clsact
ip netns exec "${ns1}" tc filter add dev veth1 egress \
	bpf direct-action object-file ./test_tc_tunnel.o \
	section "encap_${tuntype}"
echo "test bpf encap without decap (expect failure)"
server_listen
! client_connect

# serverside, insert decap module
# server is still running
# client can connect again
ip netns exec "${ns2}" ip link add dev testtun0 type "${tuntype}" \
	remote "${addr1}" local "${addr2}"
ip netns exec "${ns2}" ip link set dev testtun0 up
echo "test bpf encap with tunnel device decap"
client_connect
verify_data

# serverside, use BPF for decap
ip netns exec "${ns2}" ip link del dev testtun0
ip netns exec "${ns2}" tc qdisc add dev veth2 clsact
ip netns exec "${ns2}" tc filter add dev veth2 ingress \
	bpf direct-action object-file ./test_tc_tunnel.o section decap
server_listen
echo "test bpf encap with bpf decap"
client_connect
verify_data

echo OK

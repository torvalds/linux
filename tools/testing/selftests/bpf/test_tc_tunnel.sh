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

setup() {
	ip netns add "${ns1}"
	ip netns add "${ns2}"

	ip link add dev veth1 mtu 1500 netns "${ns1}" type veth \
	      peer name veth2 mtu 1500 netns "${ns2}"

	ip -netns "${ns1}" link set veth1 up
	ip -netns "${ns2}" link set veth2 up

	ip -netns "${ns1}" -4 addr add "${ns1_v4}/24" dev veth1
	ip -netns "${ns2}" -4 addr add "${ns2_v4}/24" dev veth2

	sleep 1
}

cleanup() {
	ip netns del "${ns2}"
	ip netns del "${ns1}"
}

server_listen() {
	ip netns exec "${ns2}" nc -l -p "${port}" &
	sleep 0.2
}

client_connect() {
	ip netns exec "${ns1}" nc -z -w 1 "${ns2_v4}" "${port}"
	echo $?
}

set -e
trap cleanup EXIT

setup

# basic communication works
echo "test basic connectivity"
server_listen
client_connect

# clientside, insert bpf program to encap all TCP to port ${port}
# client can no longer connect
ip netns exec "${ns1}" tc qdisc add dev veth1 clsact
ip netns exec "${ns1}" tc filter add dev veth1 egress \
	bpf direct-action object-file ./test_tc_tunnel.o section encap
echo "test bpf encap without decap (expect failure)"
server_listen
! client_connect

# serverside, insert decap module
# server is still running
# client can connect again
ip netns exec "${ns2}" ip link add dev testtun0 type ipip \
	remote "${ns1_v4}" local "${ns2_v4}"
ip netns exec "${ns2}" ip link set dev testtun0 up
echo "test bpf encap with tunnel device decap"
client_connect

echo OK

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

readonly NS="ns-$(mktemp -u XXXXXX)"

cleanup() {
	ip netns del $NS
}

ip netns add $NS
trap cleanup EXIT

ip -netns $NS link set lo up
ip -netns $NS tuntap add name tap1 mode tap
ip -netns $NS link set tap1 up
ip -netns $NS link set dev tap1 addr 02:00:00:00:00:01
ip -netns $NS -6 addr add fdab::1 peer fdab::2 dev tap1 nodad
ip netns exec $NS ethtool -K tap1 gro off

# disable early demux, else udp_v6_early_demux pulls udp header into linear
ip netns exec $NS sysctl -w net.ipv4.ip_early_demux=0

echo "no filter"
ip netns exec $NS ./skf_net_off -i tap1

echo "filter, linear skb (-f)"
ip netns exec $NS ./skf_net_off -i tap1 -f

echo "filter, fragmented skb (-f) (-F)"
ip netns exec $NS ./skf_net_off -i tap1 -f -F

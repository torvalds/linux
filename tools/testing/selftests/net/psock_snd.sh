#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of packet socket send regression tests

set -e

readonly mtu=1500
readonly iphlen=20
readonly udphlen=8

readonly vnet_hlen=10
readonly eth_hlen=14

readonly mss="$((${mtu} - ${iphlen} - ${udphlen}))"
readonly mss_exceeds="$((${mss} + 1))"

readonly max_mtu=65535
readonly max_mss="$((${max_mtu} - ${iphlen} - ${udphlen}))"
readonly max_mss_exceeds="$((${max_mss} + 1))"

# functional checks (not a full cross-product)

echo "dgram"
./in_netns.sh ./psock_snd -d

echo "dgram bind"
./in_netns.sh ./psock_snd -d -b

echo "raw"
./in_netns.sh ./psock_snd

echo "raw bind"
./in_netns.sh ./psock_snd -b

echo "raw qdisc bypass"
./in_netns.sh ./psock_snd -q

echo "raw vlan"
./in_netns.sh ./psock_snd -V

echo "raw vnet hdr"
./in_netns.sh ./psock_snd -v

echo "raw csum_off"
./in_netns.sh ./psock_snd -v -c

echo "raw csum_off with bad offset (fails)"
(! ./in_netns.sh ./psock_snd -v -c -C)


# bounds check: send {max, max + 1, min, min - 1} lengths

echo "raw min size"
./in_netns.sh ./psock_snd -l 0

echo "raw mtu size"
./in_netns.sh ./psock_snd -l "${mss}"

echo "raw mtu size + 1 (fails)"
(! ./in_netns.sh ./psock_snd -l "${mss_exceeds}")

# fails due to ARPHRD_ETHER check in packet_extra_vlan_len_allowed
#
# echo "raw vlan mtu size"
# ./in_netns.sh ./psock_snd -V -l "${mss}"

echo "raw vlan mtu size + 1 (fails)"
(! ./in_netns.sh ./psock_snd -V -l "${mss_exceeds}")

echo "dgram mtu size"
./in_netns.sh ./psock_snd -d -l "${mss}"

echo "dgram mtu size + 1 (fails)"
(! ./in_netns.sh ./psock_snd -d -l "${mss_exceeds}")

echo "raw truncate hlen (fails: does not arrive)"
(! ./in_netns.sh ./psock_snd -t "$((${vnet_hlen} + ${eth_hlen}))")

echo "raw truncate hlen - 1 (fails: EINVAL)"
(! ./in_netns.sh ./psock_snd -t "$((${vnet_hlen} + ${eth_hlen} - 1))")


# gso checks: implies -l, because with gso len must exceed gso_size

echo "raw gso min size"
./in_netns.sh ./psock_snd -v -c -g -l "${mss_exceeds}"

echo "raw gso min size - 1 (fails)"
(! ./in_netns.sh ./psock_snd -v -c -g -l "${mss}")

echo "raw gso max size"
./in_netns.sh ./psock_snd -v -c -g -l "${max_mss}"

echo "raw gso max size + 1 (fails)"
(! ./in_netns.sh ./psock_snd -v -c -g -l "${max_mss_exceeds}")

echo "OK. All tests passed"

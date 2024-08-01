#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Run a series of udpgso regression tests

set -o errexit
set -o nounset

setup_loopback() {
	ip addr add dev lo 10.0.0.1/32
	ip addr add dev lo fd00::1/128 nodad noprefixroute
}

test_dev_mtu() {
	setup_loopback
	# Reduce loopback MTU
	ip link set dev lo mtu 1500
}

test_route_mtu() {
	setup_loopback
	# Remove default local routes
	ip route del local 10.0.0.1/32 table local dev lo
	ip route del local fd00::1/128 table local dev lo
	# Install local routes with reduced MTU
	ip route add local 10.0.0.1/32 table local dev lo mtu 1500
	ip route add local fd00::1/128 table local dev lo mtu 1500
}

setup_dummy_sink() {
	ip link add name sink mtu 1500 type dummy
	ip addr add dev sink 10.0.0.0/24
	ip addr add dev sink fd00::2/64 nodad
	ip link set dev sink up
}

test_hw_gso_hw_csum() {
	setup_dummy_sink
	ethtool -K sink tx-checksum-ip-generic on >/dev/null
	ethtool -K sink tx-udp-segmentation on >/dev/null
}

test_sw_gso_hw_csum() {
	setup_dummy_sink
	ethtool -K sink tx-checksum-ip-generic on >/dev/null
	ethtool -K sink tx-udp-segmentation off >/dev/null
}

test_sw_gso_sw_csum() {
	setup_dummy_sink
	ethtool -K sink tx-checksum-ip-generic off >/dev/null
	ethtool -K sink tx-udp-segmentation off >/dev/null
}

if [ "$#" -gt 0 ]; then
	"$1"
	shift 2 # pop "test_*" arg and "--" delimiter
	exec "$@"
fi

echo "ipv4 cmsg"
./in_netns.sh "$0" test_dev_mtu -- ./udpgso -4 -C

echo "ipv4 setsockopt"
./in_netns.sh "$0" test_dev_mtu -- ./udpgso -4 -C -s

echo "ipv6 cmsg"
./in_netns.sh "$0" test_dev_mtu -- ./udpgso -6 -C

echo "ipv6 setsockopt"
./in_netns.sh "$0" test_dev_mtu -- ./udpgso -6 -C -s

echo "ipv4 connected"
./in_netns.sh "$0" test_route_mtu -- ./udpgso -4 -c

echo "ipv6 connected"
./in_netns.sh "$0" test_route_mtu -- ./udpgso -6 -c

echo "ipv4 msg_more"
./in_netns.sh "$0" test_dev_mtu -- ./udpgso -4 -C -m

echo "ipv6 msg_more"
./in_netns.sh "$0" test_dev_mtu -- ./udpgso -6 -C -m

echo "ipv4 hw-gso hw-csum"
./in_netns.sh "$0" test_hw_gso_hw_csum -- ./udpgso -4 -C -R

echo "ipv6 hw-gso hw-csum"
./in_netns.sh "$0" test_hw_gso_hw_csum -- ./udpgso -6 -C -R

echo "ipv4 sw-gso hw-csum"
./in_netns.sh "$0" test_sw_gso_hw_csum -- ./udpgso -4 -C -R

echo "ipv6 sw-gso hw-csum"
./in_netns.sh "$0" test_sw_gso_hw_csum -- ./udpgso -6 -C -R

echo "ipv4 sw-gso sw-csum"
./in_netns.sh "$0" test_sw_gso_sw_csum -- ./udpgso -4 -C -R

echo "ipv6 sw-gso sw-csum"
./in_netns.sh "$0" test_sw_gso_sw_csum -- ./udpgso -6 -C -R

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# A wrapper to run VXLAN test for IPv6.

ADDR_FAMILY=ipv6
LOCAL_IP_1=2001:db8:1::1
LOCAL_IP_2=2001:db8:1::2
PREFIX_LEN=128
UDPCSUM_FLAFS="udp6zerocsumrx udp6zerocsumtx"
MC_IP=FF02::2
IP_FLAG="-6"

ALL_TESTS="
	sanitization_test
	offload_indication_test
	sanitization_vlan_aware_test
	offload_indication_vlan_aware_test
"

sanitization_single_dev_learning_enabled_ipv6_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 learning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with learning enabled"
}

sanitization_single_dev_udp_checksum_ipv6_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning \
		noudp6zerocsumrx udp6zerocsumtx ttl 20 tos inherit \
		local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_fail
	log_test "vxlan device without zero udp checksum at RX"

	ip link del dev vxlan0

	ip link add name vxlan0 up type vxlan id 10 nolearning \
		udp6zerocsumrx noudp6zerocsumtx ttl 20 tos inherit \
		local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_fail
	log_test "vxlan device without zero udp checksum at TX"

	ip link del dev vxlan0
	ip link del dev br0

}

source vxlan.sh

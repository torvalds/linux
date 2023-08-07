#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../../net/forwarding

VXPORT=4789

ALL_TESTS="
	create_vxlan_on_top_of_8021ad_bridge
"
NUM_NETIFS=2
source $lib_dir/lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

create_vxlan_on_top_of_8021ad_bridge()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 vlan_protocol 802.1ad \
		vlan_default_pvid 0 mcast_snooping 0
	ip link set dev br0 addrgenmode none
	ip link set dev br0 up

	ip link add name vx100 type vxlan id 1000 local 192.0.2.17 dstport \
		"$VXPORT" nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx100 up

	ip link set dev $swp1 master br0
	ip link set dev vx100 master br0

	bridge vlan add vid 100 dev vx100 pvid untagged 2>/dev/null
	check_fail $? "802.1ad bridge with VxLAN in Spectrum-1 not rejected"

	bridge vlan add vid 100 dev vx100 pvid untagged 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "802.1ad bridge with VxLAN in Spectrum-1 rejected without extack"

	log_test "create VxLAN on top of 802.1ad bridge"

	ip link del dev vx100
	ip link del dev br0
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../../net/forwarding

VXPORT=4789

ALL_TESTS="
	create_dot1d_and_dot1ad_vxlans
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

create_dot1d_and_dot1ad_vxlans()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 vlan_protocol 802.1ad \
		vlan_default_pvid 0 mcast_snooping 0
	ip link set dev br0 up

	ip link add name vx100 type vxlan id 1000 local 192.0.2.17 dstport \
		"$VXPORT" nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx100 up

	ip link set dev $swp1 master br0
	ip link set dev vx100 master br0
	bridge vlan add vid 100 dev vx100 pvid untagged

	ip link add dev br1 type bridge vlan_filtering 0 mcast_snooping 0
	ip link set dev br1 up

	ip link add name vx200 type vxlan id 2000 local 192.0.2.17 dstport \
		"$VXPORT" nolearning noudpcsum tos inherit ttl 100
	ip link set dev vx200 up

	ip link set dev $swp2 master br1
	ip link set dev vx200 master br1 2>/dev/null
	check_fail $? "802.1d and 802.1ad VxLANs at the same time not rejected"

	ip link set dev vx200 master br1 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "802.1d and 802.1ad VxLANs at the same time rejected without extack"

	log_test "create 802.1d and 802.1ad VxLANs"

	ip link del dev vx200
	ip link del dev br1
	ip link del dev vx100
	ip link del dev br0
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

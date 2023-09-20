#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test operations that we expect to report extended ack.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	netdev_pre_up_test
	vxlan_vlan_add_test
	vxlan_bridge_create_test
	bridge_create_test
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

netdev_pre_up_test()
{
	RET=0

	ip link add name br1 type bridge vlan_filtering 0 mcast_snooping 0
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up
	ip link add name vx1 up type vxlan id 1000 \
		local 192.0.2.17 remote 192.0.2.18 \
		dstport 4789 nolearning noudpcsum tos inherit ttl 100

	ip link set dev vx1 master br1
	check_err $?

	ip link set dev $swp1 master br1
	check_err $?

	ip link add name br2 type bridge vlan_filtering 0 mcast_snooping 0
	ip link set dev br2 addrgenmode none
	ip link set dev br2 up
	ip link add name vx2 up type vxlan id 2000 \
		local 192.0.2.17 remote 192.0.2.18 \
		dstport 4789 nolearning noudpcsum tos inherit ttl 100

	ip link set dev vx2 master br2
	check_err $?

	ip link set dev $swp2 master br2
	check_err $?

	# Unsupported configuration: mlxsw demands that all offloaded VXLAN
	# devices have the same TTL.
	ip link set dev vx2 down
	ip link set dev vx2 type vxlan ttl 200

	ip link set dev vx2 up &>/dev/null
	check_fail $?

	ip link set dev vx2 up 2>&1 >/dev/null | grep -q mlxsw_spectrum
	check_err $?

	log_test "extack - NETDEV_PRE_UP"

	ip link del dev vx2
	ip link del dev br2

	ip link del dev vx1
	ip link del dev br1
}

vxlan_vlan_add_test()
{
	RET=0

	ip link add name br1 type bridge vlan_filtering 1 mcast_snooping 0
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up

	# Unsupported configuration: mlxsw demands VXLAN with "noudpcsum".
	ip link add name vx1 up type vxlan id 1000 \
		local 192.0.2.17 remote 192.0.2.18 \
		dstport 4789 tos inherit ttl 100

	ip link set dev vx1 master br1
	check_err $?

	bridge vlan add dev vx1 vid 1
	check_err $?

	ip link set dev $swp1 master br1
	check_err $?

	bridge vlan add dev vx1 vid 1 pvid untagged 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $?

	log_test "extack - map VLAN at VXLAN device"

	ip link del dev vx1
	ip link del dev br1
}

vxlan_bridge_create_test()
{
	RET=0

	# Unsupported configuration: mlxsw demands VXLAN with "noudpcsum".
	ip link add name vx1 up type vxlan id 1000 \
		local 192.0.2.17 remote 192.0.2.18 \
		dstport 4789 tos inherit ttl 100

	# Test with VLAN-aware bridge.
	ip link add name br1 type bridge vlan_filtering 1 mcast_snooping 0
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up

	ip link set dev vx1 master br1

	ip link set dev $swp1 master br1 2>&1 > /dev/null \
		| grep -q mlxsw_spectrum
	check_err $?

	# Test with VLAN-unaware bridge.
	ip link set dev br1 type bridge vlan_filtering 0

	ip link set dev $swp1 master br1 2>&1 > /dev/null \
		| grep -q mlxsw_spectrum
	check_err $?

	log_test "extack - bridge creation with VXLAN"

	ip link del dev br1
	ip link del dev vx1
}

bridge_create_test()
{
	RET=0

	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 addrgenmode none
	ip link set dev br1 up
	ip link add name br2 type bridge vlan_filtering 1
	ip link set dev br2 addrgenmode none
	ip link set dev br2 up

	ip link set dev $swp1 master br1
	check_err $?

	# Only one VLAN-aware bridge is supported, so this should fail with
	# an extack.
	ip link set dev $swp2 master br2 2>&1 > /dev/null \
		| grep -q mlxsw_spectrum
	check_err $?

	log_test "extack - multiple VLAN-aware bridges creation"

	ip link del dev br2
	ip link del dev br1
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

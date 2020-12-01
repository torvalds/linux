#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	create_8021ad_vlan_upper_on_top_front_panel_port
	create_8021ad_vlan_upper_on_top_bridge_port
	create_8021ad_vlan_upper_on_top_lag
	create_8021ad_vlan_upper_on_top_bridge
	create_8021ad_vlan_upper_on_top_8021ad_bridge
	create_vlan_upper_on_top_8021ad_bridge
	create_vlan_upper_on_top_front_panel_enslaved_to_8021ad_bridge
	create_vlan_upper_on_top_lag_enslaved_to_8021ad_bridge
	enslave_front_panel_with_vlan_upper_to_8021ad_bridge
	enslave_lag_with_vlan_upper_to_8021ad_bridge
	add_ip_address_to_8021ad_bridge
	switch_bridge_protocol_from_8021q_to_8021ad
"
NUM_NETIFS=2
source $lib_dir/lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	ip link set dev $swp1 up
	ip link set dev $swp2 up

	sleep 10
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

create_vlan_upper_on_top_of_bridge()
{
	RET=0

	local bridge_proto=$1; shift
	local netdev_proto=$1; shift

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol $bridge_proto vlan_default_pvid 0 mcast_snooping 0

	ip link set dev br0 up
	ip link set dev $swp1 master br0

	ip link add name br0.100 link br0 type vlan \
		protocol $netdev_proto id 100 2>/dev/null
	check_fail $? "$netdev_proto vlan upper creation on top of an $bridge_proto bridge not rejected"

	ip link add name br0.100 link br0 type vlan \
		protocol $netdev_proto id 100 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "$netdev_proto vlan upper creation on top of an $bridge_proto bridge rejected without extack"

	log_test "create $netdev_proto vlan upper on top $bridge_proto bridge"

	ip link del dev br0
}

create_8021ad_vlan_upper_on_top_front_panel_port()
{
	RET=0

	ip link add name $swp1.100 link $swp1 type vlan \
		protocol 802.1ad id 100 2>/dev/null
	check_fail $? "802.1ad vlan upper creation on top of a front panel not rejected"

	ip link add name $swp1.100 link $swp1 type vlan \
		protocol 802.1ad id 100 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "802.1ad vlan upper creation on top of a front panel rejected without extack"

	log_test "create 802.1ad vlan upper on top of a front panel"
}

create_8021ad_vlan_upper_on_top_bridge_port()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_default_pvid 0 mcast_snooping 0

	ip link set dev $swp1 master br0
	ip link set dev br0 up

	ip link add name $swp1.100 link $swp1 type vlan \
		protocol 802.1ad id 100 2>/dev/null
	check_fail $? "802.1ad vlan upper creation on top of a bridge port not rejected"

	ip link add name $swp1.100 link $swp1 type vlan \
		protocol 802.1ad id 100 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "802.1ad vlan upper creation on top of a bridge port rejected without extack"

	log_test "create 802.1ad vlan upper on top of a bridge port"

	ip link del dev br0
}

create_8021ad_vlan_upper_on_top_lag()
{
	RET=0

	ip link add name bond1 type bond mode 802.3ad
	ip link set dev $swp1 down
	ip link set dev $swp1 master bond1

	ip link add name bond1.100 link bond1 type vlan \
		protocol 802.1ad id 100 2>/dev/null
	check_fail $? "802.1ad vlan upper creation on top of a lag not rejected"

	ip link add name bond1.100 link bond1 type vlan \
		protocol 802.1ad id 100 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "802.1ad vlan upper creation on top of a lag rejected without extack"

	log_test "create 802.1ad vlan upper on top of a lag"

	ip link del dev bond1
}

create_8021ad_vlan_upper_on_top_bridge()
{
	RET=0

	create_vlan_upper_on_top_of_bridge "802.1q" "802.1ad"
}

create_8021ad_vlan_upper_on_top_8021ad_bridge()
{
	RET=0

	create_vlan_upper_on_top_of_bridge "802.1ad" "802.1ad"
}

create_vlan_upper_on_top_8021ad_bridge()
{
	RET=0

	create_vlan_upper_on_top_of_bridge "802.1ad" "802.1q"
}

create_vlan_upper_on_top_front_panel_enslaved_to_8021ad_bridge()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0
	ip link set dev br0 up

	ip link set dev $swp1 master br0

	ip link add name $swp1.100 link $swp1 type vlan id 100 2>/dev/null
	check_fail $? "vlan upper creation on top of front panel enslaved to 802.1ad bridge not rejected"

	ip link add name $swp1.100 link $swp1 type vlan id 100 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "vlan upper creation on top of front panel enslaved to 802.1ad bridge rejected without extack"

	log_test "create vlan upper on top of front panel enslaved to 802.1ad bridge"

	ip link del dev br0
}

create_vlan_upper_on_top_lag_enslaved_to_8021ad_bridge()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0
	ip link set dev br0 up

	ip link add name bond1 type bond mode 802.3ad
	ip link set dev $swp1 down
	ip link set dev $swp1 master bond1
	ip link set dev bond1 master br0

	ip link add name bond1.100 link bond1 type vlan id 100 2>/dev/null
	check_fail $? "vlan upper creation on top of lag enslaved to 802.1ad bridge not rejected"

	ip link add name bond1.100 link bond1 type vlan id 100 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "vlan upper creation on top of lag enslaved to 802.1ad bridge rejected without extack"

	log_test "create vlan upper on top of lag enslaved to 802.1ad bridge"

	ip link del dev bond1
	ip link del dev br0
}

enslave_front_panel_with_vlan_upper_to_8021ad_bridge()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0
	ip link set dev br0 up

	ip link add name $swp1.100 link $swp1 type vlan id 100

	ip link set dev $swp1 master br0 2>/dev/null
	check_fail $? "front panel with vlan upper enslavemnt to 802.1ad bridge not rejected"

	ip link set dev $swp1 master br0 2>&1 >/dev/null | grep -q mlxsw_spectrum
	check_err $? "front panel with vlan upper enslavemnt to 802.1ad bridge rejected without extack"

	log_test "enslave front panel with vlan upper to 802.1ad bridge"

	ip link del dev $swp1.100
	ip link del dev br0
}

enslave_lag_with_vlan_upper_to_8021ad_bridge()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0
	ip link set dev br0 up

	ip link add name bond1 type bond mode 802.3ad
	ip link set dev $swp1 down
	ip link set dev $swp1 master bond1
	ip link add name bond1.100 link bond1 type vlan id 100

	ip link set dev bond1 master br0 2>/dev/null
	check_fail $? "lag with vlan upper enslavemnt to 802.1ad bridge not rejected"

	ip link set dev bond1 master br0 2>&1 >/dev/null \
		| grep -q mlxsw_spectrum
	check_err $? "lag with vlan upper enslavemnt to 802.1ad bridge rejected without extack"

	log_test "enslave lag with vlan upper to 802.1ad bridge"

	ip link del dev bond1
	ip link del dev br0
}


add_ip_address_to_8021ad_bridge()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0

	ip link set dev br0 up
	ip link set dev $swp1 master br0

	ip addr add dev br0 192.0.2.17/28 2>/dev/null
	check_fail $? "IP address addition to 802.1ad bridge not rejected"

	ip addr add dev br0 192.0.2.17/28 2>&1 >/dev/null | grep -q mlxsw_spectrum
	check_err $? "IP address addition to 802.1ad bridge rejected without extack"

	log_test "IP address addition to 802.1ad bridge"

	ip link del dev br0
}

switch_bridge_protocol_from_8021q_to_8021ad()
{
	RET=0

	ip link add dev br0 type bridge vlan_filtering 1 \
		vlan_protocol 802.1ad vlan_default_pvid 0 mcast_snooping 0

	ip link set dev br0 up
	ip link set dev $swp1 master br0

	ip link set dev br0 type bridge vlan_protocol 802.1q 2>/dev/null
	check_fail $? "switching bridge protocol from 802.1q to 802.1ad not rejected"

	log_test "switch bridge protocol"

	ip link del dev br0
}


trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

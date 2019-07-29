#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test various aspects of VxLAN offloading which are specific to mlxsw, such
# as sanitization of invalid configurations and offload indication.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="sanitization_test offload_indication_test \
	sanitization_vlan_aware_test offload_indication_vlan_aware_test"
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

sanitization_single_dev_test_pass()
{
	ip link set dev $swp1 master br0
	check_err $?
	ip link set dev vxlan0 master br0
	check_err $?

	ip link set dev $swp1 nomaster

	ip link set dev $swp1 master br0
	check_err $?
}

sanitization_single_dev_test_fail()
{
	ip link set dev $swp1 master br0
	check_err $?
	ip link set dev vxlan0 master br0 &> /dev/null
	check_fail $?

	ip link set dev $swp1 nomaster

	ip link set dev vxlan0 master br0
	check_err $?
	ip link set dev $swp1 master br0 &> /dev/null
	check_fail $?
}

sanitization_single_dev_valid_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_pass

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device - valid configuration"
}

sanitization_single_dev_vlan_aware_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0 vlan_filtering 1

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_pass

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with a vlan-aware bridge"
}

sanitization_single_dev_mcast_enabled_test()
{
	RET=0

	ip link add dev br0 type bridge

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with a multicast enabled bridge"
}

sanitization_single_dev_mcast_group_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789 \
		dev $swp2 group 239.0.0.1

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with a multicast group"
}

sanitization_single_dev_no_local_ip_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with no local ip"
}

sanitization_single_dev_local_ipv6_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 2001:db8::1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with local ipv6 address"
}

sanitization_single_dev_learning_enabled_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 learning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_pass

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with learning enabled"
}

sanitization_single_dev_local_interface_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789 dev $swp2

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with local interface"
}

sanitization_single_dev_port_range_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789 \
		srcport 4000 5000

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with udp source port range"
}

sanitization_single_dev_tos_static_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos 20 local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with static tos"
}

sanitization_single_dev_ttl_inherit_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl inherit tos inherit local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with inherit ttl"
}

sanitization_single_dev_udp_checksum_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning udpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with udp checksum"
}

sanitization_single_dev_test()
{
	# These tests make sure that we correctly sanitize VxLAN device
	# configurations we do not support
	sanitization_single_dev_valid_test
	sanitization_single_dev_vlan_aware_test
	sanitization_single_dev_mcast_enabled_test
	sanitization_single_dev_mcast_group_test
	sanitization_single_dev_no_local_ip_test
	sanitization_single_dev_local_ipv6_test
	sanitization_single_dev_learning_enabled_test
	sanitization_single_dev_local_interface_test
	sanitization_single_dev_port_range_test
	sanitization_single_dev_tos_static_test
	sanitization_single_dev_ttl_inherit_test
	sanitization_single_dev_udp_checksum_test
}

sanitization_multi_devs_test_pass()
{
	ip link set dev $swp1 master br0
	check_err $?
	ip link set dev vxlan0 master br0
	check_err $?
	ip link set dev $swp2 master br1
	check_err $?
	ip link set dev vxlan1 master br1
	check_err $?

	ip link set dev $swp2 nomaster
	ip link set dev $swp1 nomaster

	ip link set dev $swp1 master br0
	check_err $?
	ip link set dev $swp2 master br1
	check_err $?
}

sanitization_multi_devs_test_fail()
{
	ip link set dev $swp1 master br0
	check_err $?
	ip link set dev vxlan0 master br0
	check_err $?
	ip link set dev $swp2 master br1
	check_err $?
	ip link set dev vxlan1 master br1 &> /dev/null
	check_fail $?

	ip link set dev $swp2 nomaster
	ip link set dev $swp1 nomaster

	ip link set dev vxlan1 master br1
	check_err $?
	ip link set dev $swp1 master br0
	check_err $?
	ip link set dev $swp2 master br1 &> /dev/null
	check_fail $?
}

sanitization_multi_devs_valid_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0
	ip link add dev br1 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	sanitization_multi_devs_test_pass

	ip link del dev vxlan1
	ip link del dev vxlan0
	ip link del dev br1
	ip link del dev br0

	log_test "multiple vxlan devices - valid configuration"
}

sanitization_multi_devs_ttl_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0
	ip link add dev br1 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning noudpcsum \
		ttl 40 tos inherit local 198.51.100.1 dstport 4789

	sanitization_multi_devs_test_fail

	ip link del dev vxlan1
	ip link del dev vxlan0
	ip link del dev br1
	ip link del dev br0

	log_test "multiple vxlan devices with different ttl"
}

sanitization_multi_devs_udp_dstport_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0
	ip link add dev br1 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 5789

	sanitization_multi_devs_test_fail

	ip link del dev vxlan1
	ip link del dev vxlan0
	ip link del dev br1
	ip link del dev br0

	log_test "multiple vxlan devices with different udp destination port"
}

sanitization_multi_devs_local_ip_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0
	ip link add dev br1 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.2 dstport 4789

	sanitization_multi_devs_test_fail

	ip link del dev vxlan1
	ip link del dev vxlan0
	ip link del dev br1
	ip link del dev br0

	log_test "multiple vxlan devices with different local ip"
}

sanitization_multi_devs_test()
{
	# The device has a single VTEP, which means all the VxLAN devices
	# we offload must share certain properties such as source IP and
	# UDP destination port. These tests make sure that we forbid
	# configurations that violate this limitation
	sanitization_multi_devs_valid_test
	sanitization_multi_devs_ttl_test
	sanitization_multi_devs_udp_dstport_test
	sanitization_multi_devs_local_ip_test
}

sanitization_test()
{
	sanitization_single_dev_test
	sanitization_multi_devs_test
}

offload_indication_setup_create()
{
	# Create a simple setup with two bridges, each with a VxLAN device
	# and one local port
	ip link add name br0 up type bridge mcast_snooping 0
	ip link add name br1 up type bridge mcast_snooping 0

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br1

	ip address add 198.51.100.1/32 dev lo

	ip link add name vxlan0 up master br0 type vxlan id 10 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan1 up master br1 type vxlan id 20 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789
}

offload_indication_setup_destroy()
{
	ip link del dev vxlan1
	ip link del dev vxlan0

	ip address del 198.51.100.1/32 dev lo

	ip link set dev $swp2 nomaster
	ip link set dev $swp1 nomaster

	ip link del dev br1
	ip link del dev br0
}

offload_indication_fdb_flood_test()
{
	RET=0

	bridge fdb append 00:00:00:00:00:00 dev vxlan0 self dst 198.51.100.2

	bridge fdb show brport vxlan0 | grep 00:00:00:00:00:00 \
		| grep -q offload
	check_err $?

	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self

	log_test "vxlan flood entry offload indication"
}

offload_indication_fdb_bridge_test()
{
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan0 self master static \
		dst 198.51.100.2

	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_err $?
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_err $?

	log_test "vxlan entry offload indication - initial state"

	# Remove FDB entry from the bridge driver and check that corresponding
	# entry in the VxLAN driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan0 master
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_fail $?

	log_test "vxlan entry offload indication - after removal from bridge"

	# Add the FDB entry back to the bridge driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan0 master static
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_err $?
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_err $?

	log_test "vxlan entry offload indication - after re-add to bridge"

	# Remove FDB entry from the VxLAN driver and check that corresponding
	# entry in the bridge driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan0 self
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_fail $?

	log_test "vxlan entry offload indication - after removal from vxlan"

	# Add the FDB entry back to the VxLAN driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan0 self dst 198.51.100.2
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_err $?
	bridge fdb show brport vxlan0 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_err $?

	log_test "vxlan entry offload indication - after re-add to vxlan"

	bridge fdb del de:ad:be:ef:13:37 dev vxlan0 self master
}

offload_indication_fdb_test()
{
	offload_indication_fdb_flood_test
	offload_indication_fdb_bridge_test
}

offload_indication_decap_route_test()
{
	RET=0

	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link set dev vxlan0 down
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link set dev vxlan1 down
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_fail $?

	log_test "vxlan decap route - vxlan device down"

	RET=0

	ip link set dev vxlan1 up
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link set dev vxlan0 up
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	log_test "vxlan decap route - vxlan device up"

	RET=0

	ip address delete 198.51.100.1/32 dev lo
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_fail $?

	ip address add 198.51.100.1/32 dev lo
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	log_test "vxlan decap route - add local route"

	RET=0

	ip link set dev $swp1 nomaster
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link set dev $swp2 nomaster
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_fail $?

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br1
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	log_test "vxlan decap route - local ports enslavement"

	RET=0

	ip link del dev br0
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link del dev br1
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_fail $?

	log_test "vxlan decap route - bridge device deletion"

	RET=0

	ip link add name br0 up type bridge mcast_snooping 0
	ip link add name br1 up type bridge mcast_snooping 0
	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br1
	ip link set dev vxlan0 master br0
	ip link set dev vxlan1 master br1
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link del dev vxlan0
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	ip link del dev vxlan1
	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_fail $?

	log_test "vxlan decap route - vxlan device deletion"

	ip link add name vxlan0 up master br0 type vxlan id 10 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan1 up master br1 type vxlan id 20 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789
}

check_fdb_offloaded()
{
	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb show dev vxlan0 | grep $mac | grep self | grep -q offload
	check_err $?
	bridge fdb show dev vxlan0 | grep $mac | grep master | grep -q offload
	check_err $?

	bridge fdb show dev vxlan0 | grep $zmac | grep self | grep -q offload
	check_err $?
}

check_vxlan_fdb_not_offloaded()
{
	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb show dev vxlan0 | grep $mac | grep -q self
	check_err $?
	bridge fdb show dev vxlan0 | grep $mac | grep self | grep -q offload
	check_fail $?

	bridge fdb show dev vxlan0 | grep $zmac | grep -q self
	check_err $?
	bridge fdb show dev vxlan0 | grep $zmac | grep self | grep -q offload
	check_fail $?
}

check_bridge_fdb_not_offloaded()
{
	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb show dev vxlan0 | grep $mac | grep -q master
	check_err $?
	bridge fdb show dev vxlan0 | grep $mac | grep master | grep -q offload
	check_fail $?
}

__offload_indication_join_vxlan_first()
{
	local vid=$1; shift

	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb append $zmac dev vxlan0 self dst 198.51.100.2

	ip link set dev vxlan0 master br0
	bridge fdb add dev vxlan0 $mac self master static dst 198.51.100.2

	RET=0
	check_vxlan_fdb_not_offloaded
	ip link set dev $swp1 master br0
	sleep .1
	check_fdb_offloaded
	log_test "offload indication - attach vxlan first"

	RET=0
	ip link set dev vxlan0 down
	check_vxlan_fdb_not_offloaded
	check_bridge_fdb_not_offloaded
	log_test "offload indication - set vxlan down"

	RET=0
	ip link set dev vxlan0 up
	sleep .1
	check_fdb_offloaded
	log_test "offload indication - set vxlan up"

	if [[ ! -z $vid ]]; then
		RET=0
		bridge vlan del dev vxlan0 vid $vid
		check_vxlan_fdb_not_offloaded
		check_bridge_fdb_not_offloaded
		log_test "offload indication - delete VLAN"

		RET=0
		bridge vlan add dev vxlan0 vid $vid
		check_vxlan_fdb_not_offloaded
		check_bridge_fdb_not_offloaded
		log_test "offload indication - add tagged VLAN"

		RET=0
		bridge vlan add dev vxlan0 vid $vid pvid untagged
		sleep .1
		check_fdb_offloaded
		log_test "offload indication - add pvid/untagged VLAN"
	fi

	RET=0
	ip link set dev $swp1 nomaster
	check_vxlan_fdb_not_offloaded
	log_test "offload indication - detach port"
}

offload_indication_join_vxlan_first()
{
	ip link add dev br0 up type bridge mcast_snooping 0
	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	__offload_indication_join_vxlan_first

	ip link del dev vxlan0
	ip link del dev br0
}

__offload_indication_join_vxlan_last()
{
	local zmac=00:00:00:00:00:00

	RET=0

	bridge fdb append $zmac dev vxlan0 self dst 198.51.100.2

	ip link set dev $swp1 master br0

	bridge fdb show dev vxlan0 | grep $zmac | grep self | grep -q offload
	check_fail $?

	ip link set dev vxlan0 master br0

	bridge fdb show dev vxlan0 | grep $zmac | grep self | grep -q offload
	check_err $?

	log_test "offload indication - attach vxlan last"
}

offload_indication_join_vxlan_last()
{
	ip link add dev br0 up type bridge mcast_snooping 0
	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	__offload_indication_join_vxlan_last

	ip link del dev vxlan0
	ip link del dev br0
}

offload_indication_test()
{
	offload_indication_setup_create
	offload_indication_fdb_test
	offload_indication_decap_route_test
	offload_indication_setup_destroy

	log_info "offload indication - replay & cleanup"
	offload_indication_join_vxlan_first
	offload_indication_join_vxlan_last
}

sanitization_vlan_aware_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0 vlan_filtering 1

	ip link add name vxlan10 up master br0 type vxlan id 10 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789

	ip link add name vxlan20 up master br0 type vxlan id 20 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789

	# Test that when each VNI is mapped to a different VLAN we can enslave
	# a port to the bridge
	bridge vlan add vid 10 dev vxlan10 pvid untagged
	bridge vlan add vid 20 dev vxlan20 pvid untagged

	ip link set dev $swp1 master br0
	check_err $?

	log_test "vlan-aware - enslavement to vlan-aware bridge"

	# Try to map both VNIs to the same VLAN and make sure configuration
	# fails
	RET=0

	bridge vlan add vid 10 dev vxlan20 pvid untagged &> /dev/null
	check_fail $?

	log_test "vlan-aware - two vnis mapped to the same vlan"

	# Test that enslavement of a port to a bridge fails when two VNIs
	# are mapped to the same VLAN
	RET=0

	ip link set dev $swp1 nomaster

	bridge vlan del vid 20 dev vxlan20 pvid untagged
	bridge vlan add vid 10 dev vxlan20 pvid untagged

	ip link set dev $swp1 master br0 &> /dev/null
	check_fail $?

	log_test "vlan-aware - failed enslavement to vlan-aware bridge"

	bridge vlan del vid 10 dev vxlan20
	bridge vlan add vid 20 dev vxlan20 pvid untagged

	# Test that offloading of an unsupported tunnel fails when it is
	# triggered by addition of VLAN to a local port
	RET=0

	# TOS must be set to inherit
	ip link set dev vxlan10 type vxlan tos 42

	ip link set dev $swp1 master br0
	bridge vlan add vid 10 dev $swp1 &> /dev/null
	check_fail $?

	log_test "vlan-aware - failed vlan addition to a local port"

	ip link set dev vxlan10 type vxlan tos inherit

	ip link del dev vxlan20
	ip link del dev vxlan10
	ip link del dev br0
}

offload_indication_vlan_aware_setup_create()
{
	# Create a simple setup with two VxLAN devices and a single VLAN-aware
	# bridge
	ip link add name br0 up type bridge mcast_snooping 0 vlan_filtering 1 \
		vlan_default_pvid 0

	ip link set dev $swp1 master br0

	bridge vlan add vid 10 dev $swp1
	bridge vlan add vid 20 dev $swp1

	ip address add 198.51.100.1/32 dev lo

	ip link add name vxlan10 up master br0 type vxlan id 10 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789
	ip link add name vxlan20 up master br0 type vxlan id 20 nolearning \
		noudpcsum ttl 20 tos inherit local 198.51.100.1 dstport 4789

	bridge vlan add vid 10 dev vxlan10 pvid untagged
	bridge vlan add vid 20 dev vxlan20 pvid untagged
}

offload_indication_vlan_aware_setup_destroy()
{
	bridge vlan del vid 20 dev vxlan20
	bridge vlan del vid 10 dev vxlan10

	ip link del dev vxlan20
	ip link del dev vxlan10

	ip address del 198.51.100.1/32 dev lo

	bridge vlan del vid 20 dev $swp1
	bridge vlan del vid 10 dev $swp1

	ip link set dev $swp1 nomaster

	ip link del dev br0
}

offload_indication_vlan_aware_fdb_test()
{
	RET=0

	log_info "vxlan entry offload indication - vlan-aware"

	bridge fdb add de:ad:be:ef:13:37 dev vxlan10 self master static \
		dst 198.51.100.2 vlan 10

	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_err $?
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_err $?

	log_test "vxlan entry offload indication - initial state"

	# Remove FDB entry from the bridge driver and check that corresponding
	# entry in the VxLAN driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan10 master vlan 10
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_fail $?

	log_test "vxlan entry offload indication - after removal from bridge"

	# Add the FDB entry back to the bridge driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan10 master static vlan 10
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_err $?
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_err $?

	log_test "vxlan entry offload indication - after re-add to bridge"

	# Remove FDB entry from the VxLAN driver and check that corresponding
	# entry in the bridge driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan10 self
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_fail $?

	log_test "vxlan entry offload indication - after removal from vxlan"

	# Add the FDB entry back to the VxLAN driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan10 self dst 198.51.100.2
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep self \
		| grep -q offload
	check_err $?
	bridge fdb show brport vxlan10 | grep de:ad:be:ef:13:37 | grep -v self \
		| grep -q offload
	check_err $?

	log_test "vxlan entry offload indication - after re-add to vxlan"

	bridge fdb del de:ad:be:ef:13:37 dev vxlan10 self master vlan 10
}

offload_indication_vlan_aware_decap_route_test()
{
	RET=0

	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	# Toggle PVID flag on one VxLAN device and make sure route is still
	# marked as offloaded
	bridge vlan add vid 10 dev vxlan10 untagged

	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	# Toggle PVID flag on second VxLAN device and make sure route is no
	# longer marked as offloaded
	bridge vlan add vid 20 dev vxlan20 untagged

	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_fail $?

	# Toggle PVID flag back and make sure route is marked as offloaded
	bridge vlan add vid 10 dev vxlan10 pvid untagged
	bridge vlan add vid 20 dev vxlan20 pvid untagged

	ip route show table local | grep 198.51.100.1 | grep -q offload
	check_err $?

	log_test "vxlan decap route - vni map/unmap"
}

offload_indication_vlan_aware_join_vxlan_first()
{
	ip link add dev br0 up type bridge mcast_snooping 0 \
		vlan_filtering 1 vlan_default_pvid 1
	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	__offload_indication_join_vxlan_first 1

	ip link del dev vxlan0
	ip link del dev br0
}

offload_indication_vlan_aware_join_vxlan_last()
{
	ip link add dev br0 up type bridge mcast_snooping 0 \
		vlan_filtering 1 vlan_default_pvid 1
	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	__offload_indication_join_vxlan_last

	ip link del dev vxlan0
	ip link del dev br0
}

offload_indication_vlan_aware_l3vni_test()
{
	local zmac=00:00:00:00:00:00

	RET=0

	sysctl_set net.ipv6.conf.default.disable_ipv6 1
	ip link add dev br0 up type bridge mcast_snooping 0 \
		vlan_filtering 1 vlan_default_pvid 0
	ip link add name vxlan0 up type vxlan id 10 nolearning noudpcsum \
		ttl 20 tos inherit local 198.51.100.1 dstport 4789

	ip link set dev $swp1 master br0

	# The test will use the offload indication on the FDB entry to
	# understand if the tunnel is offloaded or not
	bridge fdb append $zmac dev vxlan0 self dst 192.0.2.1

	ip link set dev vxlan0 master br0
	bridge vlan add dev vxlan0 vid 10 pvid untagged

	# No local port or router port is member in the VLAN, so tunnel should
	# not be offloaded
	bridge fdb show brport vxlan0 | grep $zmac | grep self \
		| grep -q offload
	check_fail $? "vxlan tunnel offloaded when should not"

	# Configure a VLAN interface and make sure tunnel is offloaded
	ip link add link br0 name br10 up type vlan id 10
	sysctl_set net.ipv6.conf.br10.disable_ipv6 0
	ip -6 address add 2001:db8:1::1/64 dev br10
	bridge fdb show brport vxlan0 | grep $zmac | grep self \
		| grep -q offload
	check_err $? "vxlan tunnel not offloaded when should"

	# Unlink the VXLAN device, make sure tunnel is no longer offloaded,
	# then add it back to the bridge and make sure it is offloaded
	ip link set dev vxlan0 nomaster
	bridge fdb show brport vxlan0 | grep $zmac | grep self \
		| grep -q offload
	check_fail $? "vxlan tunnel offloaded after unlinked from bridge"

	ip link set dev vxlan0 master br0
	bridge fdb show brport vxlan0 | grep $zmac | grep self \
		| grep -q offload
	check_fail $? "vxlan tunnel offloaded despite no matching vid"

	bridge vlan add dev vxlan0 vid 10 pvid untagged
	bridge fdb show brport vxlan0 | grep $zmac | grep self \
		| grep -q offload
	check_err $? "vxlan tunnel not offloaded after adding vid"

	log_test "vxlan - l3 vni"

	ip link del dev vxlan0
	ip link del dev br0
	sysctl_restore net.ipv6.conf.default.disable_ipv6
}

offload_indication_vlan_aware_test()
{
	offload_indication_vlan_aware_setup_create
	offload_indication_vlan_aware_fdb_test
	offload_indication_vlan_aware_decap_route_test
	offload_indication_vlan_aware_setup_destroy

	log_info "offload indication - replay & cleanup - vlan aware"
	offload_indication_vlan_aware_join_vxlan_first
	offload_indication_vlan_aware_join_vxlan_last
	offload_indication_vlan_aware_l3vni_test
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

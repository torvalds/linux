#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test various aspects of VxLAN offloading which are specific to mlxsw, such
# as sanitization of invalid configurations and offload indication.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="sanitization_test offload_indication_test"
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

	sanitization_single_dev_test_fail

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

	sanitization_single_dev_test_fail

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

offload_indication_test()
{
	offload_indication_setup_create
	offload_indication_fdb_test
	offload_indication_decap_route_test
	offload_indication_setup_destroy
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

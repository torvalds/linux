#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test various aspects of VxLAN offloading which are specific to mlxsw, such
# as sanitization of invalid configurations and offload indication.

: ${ADDR_FAMILY:=ipv4}
export ADDR_FAMILY

: ${LOCAL_IP_1:=198.51.100.1}
export LOCAL_IP_1

: ${LOCAL_IP_2:=198.51.100.2}
export LOCAL_IP_2

: ${PREFIX_LEN:=32}
export PREFIX_LEN

: ${UDPCSUM_FLAFS:=noudpcsum}
export UDPCSUM_FLAFS

: ${MC_IP:=239.0.0.1}
export MC_IP

: ${IP_FLAG:=""}
export IP_FLAG

: ${ALL_TESTS:="
	sanitization_test
	offload_indication_test
	sanitization_vlan_aware_test
	offload_indication_vlan_aware_test
"}

lib_dir=$(dirname $0)/../../../net/forwarding
NUM_NETIFS=2
: ${TIMEOUT:=20000} # ms
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

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_pass

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device - valid configuration"
}

sanitization_single_dev_vlan_aware_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0 vlan_filtering 1

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_pass

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with a vlan-aware bridge"
}

sanitization_single_dev_mcast_enabled_test()
{
	RET=0

	ip link add dev br0 type bridge

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with a multicast enabled bridge"
}

sanitization_single_dev_mcast_group_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0
	ip link add name dummy1 up type dummy

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789 \
		dev dummy1 group $MC_IP

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev dummy1
	ip link del dev br0

	log_test "vxlan device with a multicast group"
}

sanitization_single_dev_no_local_ip_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with no local ip"
}

sanitization_single_dev_learning_enabled_ipv4_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 learning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_pass

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with learning enabled"
}

sanitization_single_dev_local_interface_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0
	ip link add name dummy1 up type dummy

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789 dev dummy1

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev dummy1
	ip link del dev br0

	log_test "vxlan device with local interface"
}

sanitization_single_dev_port_range_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789 \
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

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos 20 local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with static tos"
}

sanitization_single_dev_ttl_inherit_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl inherit tos inherit local $LOCAL_IP_1 dstport 4789

	sanitization_single_dev_test_fail

	ip link del dev vxlan0
	ip link del dev br0

	log_test "vxlan device with inherit ttl"
}

sanitization_single_dev_udp_checksum_ipv4_test()
{
	RET=0

	ip link add dev br0 type bridge mcast_snooping 0

	ip link add name vxlan0 up type vxlan id 10 nolearning udpcsum \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

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
	sanitization_single_dev_learning_enabled_"$ADDR_FAMILY"_test
	sanitization_single_dev_local_interface_test
	sanitization_single_dev_port_range_test
	sanitization_single_dev_tos_static_test
	sanitization_single_dev_ttl_inherit_test
	sanitization_single_dev_udp_checksum_"$ADDR_FAMILY"_test
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

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

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

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning $UDPCSUM_FLAFS \
		ttl 40 tos inherit local $LOCAL_IP_1 dstport 4789

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

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 5789

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

	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan1 up type vxlan id 20 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_2 dstport 4789

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

	ip address add $LOCAL_IP_1/$PREFIX_LEN dev lo

	ip link add name vxlan0 up master br0 type vxlan id 10 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan1 up master br1 type vxlan id 20 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
}

offload_indication_setup_destroy()
{
	ip link del dev vxlan1
	ip link del dev vxlan0

	ip address del $LOCAL_IP_1/$PREFIX_LEN dev lo

	ip link set dev $swp2 nomaster
	ip link set dev $swp1 nomaster

	ip link del dev br1
	ip link del dev br0
}

offload_indication_fdb_flood_test()
{
	RET=0

	bridge fdb append 00:00:00:00:00:00 dev vxlan0 self dst $LOCAL_IP_2

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb 00:00:00:00:00:00 \
		bridge fdb show brport vxlan0
	check_err $?

	bridge fdb del 00:00:00:00:00:00 dev vxlan0 self

	log_test "vxlan flood entry offload indication"
}

offload_indication_fdb_bridge_test()
{
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan0 self master static \
		dst $LOCAL_IP_2

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan0
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan0
	check_err $?

	log_test "vxlan entry offload indication - initial state"

	# Remove FDB entry from the bridge driver and check that corresponding
	# entry in the VxLAN driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan0 master
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan0
	check_err $?

	log_test "vxlan entry offload indication - after removal from bridge"

	# Add the FDB entry back to the bridge driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan0 master static
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan0
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan0
	check_err $?

	log_test "vxlan entry offload indication - after re-add to bridge"

	# Remove FDB entry from the VxLAN driver and check that corresponding
	# entry in the bridge driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan0 self
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan0
	check_err $?

	log_test "vxlan entry offload indication - after removal from vxlan"

	# Add the FDB entry back to the VxLAN driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan0 self dst $LOCAL_IP_2
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan0
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan0
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

	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link set dev vxlan0 down
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link set dev vxlan1 down
	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - vxlan device down"

	RET=0

	ip link set dev vxlan1 up
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link set dev vxlan0 up
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - vxlan device up"

	RET=0

	ip address delete $LOCAL_IP_1/$PREFIX_LEN dev lo
	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip address add $LOCAL_IP_1/$PREFIX_LEN dev lo
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - add local route"

	RET=0

	ip link set dev $swp1 nomaster
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link set dev $swp2 nomaster
	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br1
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - local ports enslavement"

	RET=0

	ip link del dev br0
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link del dev br1
	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - bridge device deletion"

	RET=0

	ip link add name br0 up type bridge mcast_snooping 0
	ip link add name br1 up type bridge mcast_snooping 0
	ip link set dev $swp1 master br0
	ip link set dev $swp2 master br1
	ip link set dev vxlan0 master br0
	ip link set dev vxlan1 master br1
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link del dev vxlan0
	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	ip link del dev vxlan1
	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - vxlan device deletion"

	ip link add name vxlan0 up master br0 type vxlan id 10 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan1 up master br1 type vxlan id 20 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
}

check_fdb_offloaded()
{
	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $mac self \
		bridge fdb show dev vxlan0
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $mac master \
		bridge fdb show dev vxlan0
	check_err $?

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show dev vxlan0
	check_err $?
}

check_vxlan_fdb_not_offloaded()
{
	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb show dev vxlan0 | grep $mac | grep -q self
	check_err $?
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb $mac self \
		bridge fdb show dev vxlan0
	check_err $?

	bridge fdb show dev vxlan0 | grep $zmac | grep -q self
	check_err $?
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show dev vxlan0
	check_err $?
}

check_bridge_fdb_not_offloaded()
{
	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb show dev vxlan0 | grep $mac | grep -q master
	check_err $?
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb $mac master \
		bridge fdb show dev vxlan0
	check_err $?
}

__offload_indication_join_vxlan_first()
{
	local vid=$1; shift

	local mac=00:11:22:33:44:55
	local zmac=00:00:00:00:00:00

	bridge fdb append $zmac dev vxlan0 self dst $LOCAL_IP_2

	ip link set dev vxlan0 master br0
	bridge fdb add dev vxlan0 $mac self master static dst $LOCAL_IP_2

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
	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	__offload_indication_join_vxlan_first

	ip link del dev vxlan0
	ip link del dev br0
}

__offload_indication_join_vxlan_last()
{
	local zmac=00:00:00:00:00:00

	RET=0

	bridge fdb append $zmac dev vxlan0 self dst $LOCAL_IP_2

	ip link set dev $swp1 master br0

	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show dev vxlan0
	check_err $?

	ip link set dev vxlan0 master br0

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show dev vxlan0
	check_err $?

	log_test "offload indication - attach vxlan last"
}

offload_indication_join_vxlan_last()
{
	ip link add dev br0 up type bridge mcast_snooping 0
	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

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
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	ip link add name vxlan20 up master br0 type vxlan id 20 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

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

	# Test that when two VXLAN tunnels with conflicting configurations
	# (i.e., different TTL) are enslaved to the same VLAN-aware bridge,
	# then the enslavement of a port to the bridge is denied.

	# Use the offload indication of the local route to ensure the VXLAN
	# configuration was correctly rollbacked.
	ip address add $LOCAL_IP_1/$PREFIX_LEN dev lo

	ip link set dev vxlan10 type vxlan ttl 10
	ip link set dev $swp1 master br0 &> /dev/null
	check_fail $?

	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	log_test "vlan-aware - failed enslavement to bridge due to conflict"

	ip link set dev vxlan10 type vxlan ttl 20
	ip address del $LOCAL_IP_1/$PREFIX_LEN dev lo

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

	ip address add $LOCAL_IP_1/$PREFIX_LEN dev lo

	ip link add name vxlan10 up master br0 type vxlan id 10 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789
	ip link add name vxlan20 up master br0 type vxlan id 20 nolearning \
		$UDPCSUM_FLAFS ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	bridge vlan add vid 10 dev vxlan10 pvid untagged
	bridge vlan add vid 20 dev vxlan20 pvid untagged
}

offload_indication_vlan_aware_setup_destroy()
{
	bridge vlan del vid 20 dev vxlan20
	bridge vlan del vid 10 dev vxlan10

	ip link del dev vxlan20
	ip link del dev vxlan10

	ip address del $LOCAL_IP_1/$PREFIX_LEN dev lo

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
		dst $LOCAL_IP_2 vlan 10

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan10
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan10
	check_err $?

	log_test "vxlan entry offload indication - initial state"

	# Remove FDB entry from the bridge driver and check that corresponding
	# entry in the VxLAN driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan10 master vlan 10
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan10
	check_err $?

	log_test "vxlan entry offload indication - after removal from bridge"

	# Add the FDB entry back to the bridge driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan10 master static vlan 10
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan10
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan10
	check_err $?

	log_test "vxlan entry offload indication - after re-add to bridge"

	# Remove FDB entry from the VxLAN driver and check that corresponding
	# entry in the bridge driver is not marked as offloaded
	RET=0

	bridge fdb del de:ad:be:ef:13:37 dev vxlan10 self
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan10
	check_err $?

	log_test "vxlan entry offload indication - after removal from vxlan"

	# Add the FDB entry back to the VxLAN driver and make sure it is
	# marked as offloaded in both drivers
	RET=0

	bridge fdb add de:ad:be:ef:13:37 dev vxlan10 self dst $LOCAL_IP_2
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self bridge fdb show brport vxlan10
	check_err $?
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb \
		de:ad:be:ef:13:37 self -v bridge fdb show brport vxlan10
	check_err $?

	log_test "vxlan entry offload indication - after re-add to vxlan"

	bridge fdb del de:ad:be:ef:13:37 dev vxlan10 self master vlan 10
}

offload_indication_vlan_aware_decap_route_test()
{
	RET=0

	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	# Toggle PVID flag on one VxLAN device and make sure route is still
	# marked as offloaded
	bridge vlan add vid 10 dev vxlan10 untagged

	busywait "$TIMEOUT" wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	# Toggle PVID flag on second VxLAN device and make sure route is no
	# longer marked as offloaded
	bridge vlan add vid 20 dev vxlan20 untagged

	busywait "$TIMEOUT" not wait_for_offload \
		ip $IP_FLAG route show table local $LOCAL_IP_1
	check_err $?

	# Toggle PVID flag back and make sure route is marked as offloaded
	bridge vlan add vid 10 dev vxlan10 pvid untagged
	bridge vlan add vid 20 dev vxlan20 pvid untagged

	busywait "$TIMEOUT" wait_for_offload ip $IP_FLAG route show table local \
		$LOCAL_IP_1
	check_err $?

	log_test "vxlan decap route - vni map/unmap"
}

offload_indication_vlan_aware_join_vxlan_first()
{
	ip link add dev br0 up type bridge mcast_snooping 0 \
		vlan_filtering 1 vlan_default_pvid 1
	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	__offload_indication_join_vxlan_first 1

	ip link del dev vxlan0
	ip link del dev br0
}

offload_indication_vlan_aware_join_vxlan_last()
{
	ip link add dev br0 up type bridge mcast_snooping 0 \
		vlan_filtering 1 vlan_default_pvid 1
	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

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
	ip link add name vxlan0 up type vxlan id 10 nolearning $UDPCSUM_FLAFS \
		ttl 20 tos inherit local $LOCAL_IP_1 dstport 4789

	ip link set dev $swp1 master br0

	# The test will use the offload indication on the FDB entry to
	# understand if the tunnel is offloaded or not
	bridge fdb append $zmac dev vxlan0 self dst $LOCAL_IP_2

	ip link set dev vxlan0 master br0
	bridge vlan add dev vxlan0 vid 10 pvid untagged

	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show brport vxlan0
	check_err $? "vxlan tunnel not offloaded when should"

	# Configure a VLAN interface and make sure tunnel is offloaded
	ip link add link br0 name br10 up type vlan id 10
	sysctl_set net.ipv6.conf.br10.disable_ipv6 0
	ip -6 address add 2001:db8:1::1/64 dev br10
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show brport vxlan0
	check_err $? "vxlan tunnel not offloaded when should"

	# Unlink the VXLAN device, make sure tunnel is no longer offloaded,
	# then add it back to the bridge and make sure it is offloaded
	ip link set dev vxlan0 nomaster
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show brport vxlan0
	check_err $? "vxlan tunnel offloaded after unlinked from bridge"

	ip link set dev vxlan0 master br0
	busywait "$TIMEOUT" not wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show brport vxlan0
	check_err $? "vxlan tunnel offloaded despite no matching vid"

	bridge vlan add dev vxlan0 vid 10 pvid untagged
	busywait "$TIMEOUT" wait_for_offload grep_bridge_fdb $zmac self \
		bridge fdb show brport vxlan0
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

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test various interface configuration scenarios. Observe that configurations
# deemed valid by mlxsw succeed, invalid configurations fail and that no traces
# are produced. To prevent the test from passing in case traces are produced,
# the user can set the 'kernel.panic_on_warn' and 'kernel.panic_on_oops'
# sysctls in its environment.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	rif_vrf_set_addr_test
	rif_non_inherit_bridge_addr_test
	vlan_interface_deletion_test
	bridge_deletion_test
	bridge_vlan_flags_test
	vlan_1_test
	lag_bridge_upper_test
	duplicate_vlans_test
	vlan_rif_refcount_test
	subport_rif_refcount_test
	subport_rif_lag_join_test
	vlan_dev_deletion_test
	lag_unlink_slaves_test
	lag_dev_deletion_test
	vlan_interface_uppers_test
	bridge_extern_learn_test
	neigh_offload_test
	nexthop_offload_test
	nexthop_obj_invalid_test
	nexthop_obj_offload_test
	nexthop_obj_group_offload_test
	nexthop_obj_bucket_offload_test
	nexthop_obj_blackhole_offload_test
	nexthop_obj_route_offload_test
	bridge_locked_port_test
	devlink_reload_test
"
NUM_NETIFS=2
: ${TIMEOUT:=20000} # ms
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

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

rif_vrf_set_addr_test()
{
	# Test that it is possible to set an IP address on a VRF upper despite
	# its random MAC address.
	RET=0

	ip link add name vrf-test type vrf table 10
	ip link set dev $swp1 master vrf-test

	ip -4 address add 192.0.2.1/24 dev vrf-test
	check_err $? "failed to set IPv4 address on VRF"
	ip -6 address add 2001:db8:1::1/64 dev vrf-test
	check_err $? "failed to set IPv6 address on VRF"

	log_test "RIF - setting IP address on VRF"

	ip link del dev vrf-test
}

rif_non_inherit_bridge_addr_test()
{
	local swp2_mac=$(mac_get $swp2)

	RET=0

	# Create first RIF
	ip addr add dev $swp1 192.0.2.1/28
	check_err $?

	# Create a FID RIF
	ip link add name br1 up type bridge vlan_filtering 0
	ip link set dev br1 addr $swp2_mac
	ip link set dev $swp2 master br1
	ip addr add dev br1 192.0.2.17/28
	check_err $?

	# Prepare a device with a low MAC address
	ip link add name d up type dummy
	ip link set dev d addr 00:11:22:33:44:55

	# Attach the device to br1. Since the bridge address was set, it should
	# work.
	ip link set dev d master br1 &>/dev/null
	check_err $? "Could not attach a device with low MAC to a bridge with RIF"

	# Port MAC address change should be allowed for a bridge with set MAC.
	ip link set dev $swp2 addr 00:11:22:33:44:55
	check_err $? "Changing swp2's MAC address not permitted"

	log_test "RIF - attach port with bad MAC to bridge with set MAC"

	ip link set dev $swp2 addr $swp2_mac
	ip link del dev d
	ip link del dev br1
	ip addr del dev $swp1 192.0.2.1/28
}

vlan_interface_deletion_test()
{
	# Test that when a VLAN interface is deleted, its associated router
	# interface (RIF) is correctly deleted and not leaked. See commit
	# c360867ec46a ("mlxsw: spectrum: Delete RIF when VLAN device is
	# removed") for more details
	RET=0

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev $swp1 master br0

	ip link add link br0 name br0.10 type vlan id 10
	ip -6 address add 2001:db8:1::1/64 dev br0.10
	ip link del dev br0.10

	# If we leaked the previous RIF, then this should produce a trace
	ip link add link br0 name br0.20 type vlan id 20
	ip -6 address add 2001:db8:1::1/64 dev br0.20
	ip link del dev br0.20

	log_test "vlan interface deletion"

	ip link del dev br0
}

bridge_deletion_test()
{
	# Test that when a bridge with VLAN interfaces is deleted, we correctly
	# delete the associated RIFs. See commit 602b74eda813 ("mlxsw:
	# spectrum_switchdev: Do not leak RIFs when removing bridge") for more
	# details
	RET=0

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev $swp1 master br0
	ip -6 address add 2001:db8::1/64 dev br0

	ip link add link br0 name br0.10 type vlan id 10
	ip -6 address add 2001:db8:1::1/64 dev br0.10

	ip link add link br0 name br0.20 type vlan id 20
	ip -6 address add 2001:db8:2::1/64 dev br0.20

	ip link del dev br0

	# If we leaked previous RIFs, then this should produce a trace
	ip -6 address add 2001:db8:1::1/64 dev $swp1
	ip -6 address del 2001:db8:1::1/64 dev $swp1

	log_test "bridge deletion"
}

bridge_vlan_flags_test()
{
	# Test that when bridge VLAN flags are toggled, we do not take
	# unnecessary references on related structs. See commit 9e25826ffc94
	# ("mlxsw: spectrum_switchdev: Fix port_vlan refcounting") for more
	# details
	RET=0

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev $swp1 master br0

	bridge vlan add vid 10 dev $swp1 pvid untagged
	bridge vlan add vid 10 dev $swp1 untagged
	bridge vlan add vid 10 dev $swp1 pvid
	bridge vlan add vid 10 dev $swp1
	ip link del dev br0

	# If we did not handle references correctly, then this should produce a
	# trace
	devlink dev reload "$DEVLINK_DEV"

	# Allow netdevices to be re-created following the reload
	sleep 20

	log_test "bridge vlan flags"
}

vlan_1_test()
{
	# Test that VLAN 1 can be configured over mlxsw ports. In the past it
	# was used internally for untagged traffic. See commit 47bf9df2e820
	# ("mlxsw: spectrum: Forbid creation of VLAN 1 over port/LAG") for more
	# details
	RET=0

	ip link add link $swp1 name $swp1.1 type vlan id 1
	check_err $? "did not manage to create vlan 1 when should"

	log_test "vlan 1"

	ip link del dev $swp1.1
}

lag_bridge_upper_test()
{
	# Test that ports cannot be enslaved to LAG devices that have uppers
	# and that failure is handled gracefully. See commit b3529af6bb0d
	# ("spectrum: Reference count VLAN entries") for more details
	RET=0

	ip link add name bond1 type bond mode 802.3ad

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev bond1 master br0

	ip link set dev $swp1 down
	ip link set dev $swp1 master bond1 &> /dev/null
	check_fail $? "managed to enslave port to lag when should not"

	# This might generate a trace, if we did not handle the failure
	# correctly
	ip -6 address add 2001:db8:1::1/64 dev $swp1
	ip -6 address del 2001:db8:1::1/64 dev $swp1

	log_test "lag with bridge upper"

	ip link del dev br0
	ip link del dev bond1
}

duplicate_vlans_test()
{
	# Test that on a given port a VLAN is only used once. Either as VLAN
	# in a VLAN-aware bridge or as a VLAN device
	RET=0

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev $swp1 master br0
	bridge vlan add vid 10 dev $swp1

	ip link add link $swp1 name $swp1.10 type vlan id 10 &> /dev/null
	check_fail $? "managed to create vlan device when should not"

	bridge vlan del vid 10 dev $swp1
	ip link add link $swp1 name $swp1.10 type vlan id 10
	check_err $? "did not manage to create vlan device when should"
	bridge vlan add vid 10 dev $swp1 &> /dev/null
	check_fail $? "managed to add bridge vlan when should not"

	log_test "duplicate vlans"

	ip link del dev $swp1.10
	ip link del dev br0
}

vlan_rif_refcount_test()
{
	# Test that RIFs representing VLAN interfaces are not affected from
	# ports member in the VLAN. We use the offload indication on routes
	# configured on the RIF to understand if it was created / destroyed
	RET=0

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev $swp1 master br0

	ip link set dev $swp1 up
	ip link set dev br0 up

	ip link add link br0 name br0.10 up type vlan id 10
	ip -6 address add 2001:db8:1::1/64 dev br0.10

	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev br0.10
	check_err $? "vlan rif was not created before adding port to vlan"

	bridge vlan add vid 10 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev br0.10
	check_err $? "vlan rif was destroyed after adding port to vlan"

	bridge vlan del vid 10 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev br0.10
	check_err $? "vlan rif was destroyed after removing port from vlan"

	ip link set dev $swp1 nomaster
	busywait "$TIMEOUT" not wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev br0.10
	check_err $? "vlan rif was not destroyed after unlinking port from bridge"

	log_test "vlan rif refcount"

	ip link del dev br0.10
	ip link set dev $swp1 down
	ip link del dev br0
}

subport_rif_refcount_test()
{
	# Test that RIFs representing upper devices of physical ports are
	# reference counted correctly and destroyed when should. We use the
	# offload indication on routes configured on the RIF to understand if
	# it was created / destroyed
	RET=0

	ip link add name bond1 type bond mode 802.3ad
	ip link set dev $swp1 down
	ip link set dev $swp2 down
	ip link set dev $swp1 master bond1
	ip link set dev $swp2 master bond1

	ip link set dev bond1 up
	ip link add link bond1 name bond1.10 up type vlan id 10
	ip -6 address add 2001:db8:1::1/64 dev bond1
	ip -6 address add 2001:db8:2::1/64 dev bond1.10

	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif was not created on lag device"
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:2::2 dev bond1.10
	check_err $? "subport rif was not created on vlan device"

	ip link set dev $swp1 nomaster
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif of lag device was destroyed when should not"
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:2::2 dev bond1.10
	check_err $? "subport rif of vlan device was destroyed when should not"

	ip link set dev $swp2 nomaster
	busywait "$TIMEOUT" not wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif of lag device was not destroyed when should"
	busywait "$TIMEOUT" not wait_for_offload \
		ip -6 route get fibmatch 2001:db8:2::2 dev bond1.10
	check_err $? "subport rif of vlan device was not destroyed when should"

	log_test "subport rif refcount"

	ip link del dev bond1.10
	ip link del dev bond1
}

subport_rif_lag_join_test()
{
	# Test that the reference count of a RIF configured for a LAG is
	# incremented / decremented when ports join / leave the LAG. We use the
	# offload indication on routes configured on the RIF to understand if
	# it was created / destroyed
	RET=0

	ip link add name bond1 type bond mode 802.3ad
	ip link set dev $swp1 down
	ip link set dev $swp2 down
	ip link set dev $swp1 master bond1
	ip link set dev $swp2 master bond1

	ip link set dev bond1 up
	ip -6 address add 2001:db8:1::1/64 dev bond1

	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif was not created on lag device"

	ip link set dev $swp1 nomaster
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif of lag device was destroyed after removing one port"

	ip link set dev $swp1 master bond1
	ip link set dev $swp2 nomaster
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif of lag device was destroyed after re-adding a port and removing another"

	ip link set dev $swp1 nomaster
	busywait "$TIMEOUT" not wait_for_offload \
		ip -6 route get fibmatch 2001:db8:1::2 dev bond1
	check_err $? "subport rif of lag device was not destroyed when should"

	log_test "subport rif lag join"

	ip link del dev bond1
}

vlan_dev_deletion_test()
{
	# Test that VLAN devices are correctly deleted / unlinked when enslaved
	# to bridge
	RET=0

	ip link add name br10 type bridge
	ip link add name br20 type bridge
	ip link add name br30 type bridge
	ip link add link $swp1 name $swp1.10 type vlan id 10
	ip link add link $swp1 name $swp1.20 type vlan id 20
	ip link add link $swp1 name $swp1.30 type vlan id 30
	ip link set dev $swp1.10 master br10
	ip link set dev $swp1.20 master br20
	ip link set dev $swp1.30 master br30

	# If we did not handle the situation correctly, then these operations
	# might produce a trace
	ip link set dev $swp1.30 nomaster
	ip link del dev $swp1.20
	# Deletion via ioctl uses different code paths from netlink
	vconfig rem $swp1.10 &> /dev/null

	log_test "vlan device deletion"

	ip link del dev $swp1.30
	ip link del dev br30
	ip link del dev br20
	ip link del dev br10
}

lag_create()
{
	ip link add name bond1 type bond mode 802.3ad
	ip link set dev $swp1 down
	ip link set dev $swp2 down
	ip link set dev $swp1 master bond1
	ip link set dev $swp2 master bond1

	ip link add link bond1 name bond1.10 type vlan id 10
	ip link add link bond1 name bond1.20 type vlan id 20

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev bond1 master br0

	ip link add name br10 type bridge
	ip link set dev bond1.10 master br10

	ip link add name br20 type bridge
	ip link set dev bond1.20 master br20
}

lag_unlink_slaves_test()
{
	# Test that ports are correctly unlinked from their LAG master, when
	# the LAG and its VLAN uppers are enslaved to bridges
	RET=0

	lag_create

	ip link set dev $swp1 nomaster
	check_err $? "lag slave $swp1 was not unlinked from master"
	ip link set dev $swp2 nomaster
	check_err $? "lag slave $swp2 was not unlinked from master"

	# Try to configure corresponding VLANs as router interfaces
	ip -6 address add 2001:db8:1::1/64 dev $swp1
	check_err $? "failed to configure ip address on $swp1"

	ip link add link $swp1 name $swp1.10 type vlan id 10
	ip -6 address add 2001:db8:10::1/64 dev $swp1.10
	check_err $? "failed to configure ip address on $swp1.10"

	ip link add link $swp1 name $swp1.20 type vlan id 20
	ip -6 address add 2001:db8:20::1/64 dev $swp1.20
	check_err $? "failed to configure ip address on $swp1.20"

	log_test "lag slaves unlinking"

	ip link del dev $swp1.20
	ip link del dev $swp1.10
	ip address flush dev $swp1

	ip link del dev br20
	ip link del dev br10
	ip link del dev br0
	ip link del dev bond1
}

lag_dev_deletion_test()
{
	# Test that LAG device is correctly deleted, when the LAG and its VLAN
	# uppers are enslaved to bridges
	RET=0

	lag_create

	ip link del dev bond1

	log_test "lag device deletion"

	ip link del dev br20
	ip link del dev br10
	ip link del dev br0
}

vlan_interface_uppers_test()
{
	# Test that uppers of a VLAN interface are correctly sanitized
	RET=0

	ip link add name br0 type bridge vlan_filtering 1
	ip link set dev $swp1 master br0

	ip link add link br0 name br0.10 type vlan id 10
	ip link add link br0.10 name macvlan0 \
		type macvlan mode private &> /dev/null
	check_fail $? "managed to create a macvlan when should not"

	ip -6 address add 2001:db8:1::1/64 dev br0.10
	ip link add link br0.10 name macvlan0 type macvlan mode private
	check_err $? "did not manage to create a macvlan when should"

	ip link del dev macvlan0

	ip link add name vrf-test type vrf table 10
	ip link set dev br0.10 master vrf-test
	check_err $? "did not manage to enslave vlan interface to vrf"
	ip link del dev vrf-test

	ip link add name br-test type bridge
	ip link set dev br0.10 master br-test &> /dev/null
	check_fail $? "managed to enslave vlan interface to bridge when should not"
	ip link del dev br-test

	log_test "vlan interface uppers"

	ip link del dev br0
}

bridge_extern_learn_test()
{
	# Test that externally learned entries added from user space are
	# marked as offloaded
	RET=0

	ip link add name br0 type bridge
	ip link set dev $swp1 master br0

	bridge fdb add de:ad:be:ef:13:37 dev $swp1 master extern_learn

	busywait "$TIMEOUT" wait_for_offload \
		bridge fdb show brport $swp1 de:ad:be:ef:13:37
	check_err $? "fdb entry not marked as offloaded when should"

	log_test "externally learned fdb entry"

	ip link del dev br0
}

neigh_offload_test()
{
	# Test that IPv4 and IPv6 neighbour entries are marked as offloaded
	RET=0

	ip -4 address add 192.0.2.1/24 dev $swp1
	ip -6 address add 2001:db8:1::1/64 dev $swp1

	ip -4 neigh add 192.0.2.2 lladdr de:ad:be:ef:13:37 nud perm dev $swp1
	ip -6 neigh add 2001:db8:1::2 lladdr de:ad:be:ef:13:37 nud perm \
		dev $swp1

	busywait "$TIMEOUT" wait_for_offload \
		ip -4 neigh show dev $swp1 192.0.2.2
	check_err $? "ipv4 neigh entry not marked as offloaded when should"
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 neigh show dev $swp1 2001:db8:1::2
	check_err $? "ipv6 neigh entry not marked as offloaded when should"

	log_test "neighbour offload indication"

	ip -6 neigh del 2001:db8:1::2 dev $swp1
	ip -4 neigh del 192.0.2.2 dev $swp1
	ip -6 address del 2001:db8:1::1/64 dev $swp1
	ip -4 address del 192.0.2.1/24 dev $swp1
}

nexthop_offload_test()
{
	# Test that IPv4 and IPv6 nexthops are marked as offloaded
	RET=0

	sysctl_set net.ipv6.conf.$swp2.keep_addr_on_down 1
	simple_if_init $swp1 192.0.2.1/24 2001:db8:1::1/64
	simple_if_init $swp2 192.0.2.2/24 2001:db8:1::2/64
	setup_wait

	ip -4 route add 198.51.100.0/24 vrf v$swp1 \
		nexthop via 192.0.2.2 dev $swp1
	ip -6 route add 2001:db8:2::/64 vrf v$swp1 \
		nexthop via 2001:db8:1::2 dev $swp1

	busywait "$TIMEOUT" wait_for_offload \
		ip -4 route show 198.51.100.0/24 vrf v$swp1
	check_err $? "ipv4 nexthop not marked as offloaded when should"
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route show 2001:db8:2::/64 vrf v$swp1
	check_err $? "ipv6 nexthop not marked as offloaded when should"

	ip link set dev $swp2 down
	sleep 1

	busywait "$TIMEOUT" not wait_for_offload \
		ip -4 route show 198.51.100.0/24 vrf v$swp1
	check_err $? "ipv4 nexthop marked as offloaded when should not"
	busywait "$TIMEOUT" not wait_for_offload \
		ip -6 route show 2001:db8:2::/64 vrf v$swp1
	check_err $? "ipv6 nexthop marked as offloaded when should not"

	ip link set dev $swp2 up
	setup_wait

	busywait "$TIMEOUT" wait_for_offload \
		ip -4 route show 198.51.100.0/24 vrf v$swp1
	check_err $? "ipv4 nexthop not marked as offloaded after neigh add"
	busywait "$TIMEOUT" wait_for_offload \
		ip -6 route show 2001:db8:2::/64 vrf v$swp1
	check_err $? "ipv6 nexthop not marked as offloaded after neigh add"

	log_test "nexthop offload indication"

	ip -6 route del 2001:db8:2::/64 vrf v$swp1
	ip -4 route del 198.51.100.0/24 vrf v$swp1

	simple_if_fini $swp2 192.0.2.2/24 2001:db8:1::2/64
	simple_if_fini $swp1 192.0.2.1/24 2001:db8:1::1/64
	sysctl_restore net.ipv6.conf.$swp2.keep_addr_on_down
}

nexthop_obj_invalid_test()
{
	# Test that invalid nexthop object configurations are rejected
	RET=0

	simple_if_init $swp1 192.0.2.1/24 2001:db8:1::1/64
	simple_if_init $swp2 192.0.2.2/24 2001:db8:1::2/64
	setup_wait

	ip nexthop add id 1 via 192.0.2.3 fdb
	check_fail $? "managed to configure an FDB nexthop when should not"

	ip nexthop add id 1 encap mpls 200/300 via 192.0.2.3 dev $swp1
	check_fail $? "managed to configure a nexthop with MPLS encap when should not"

	ip nexthop add id 1 dev $swp1
	ip nexthop add id 2 dev $swp1
	ip nexthop add id 3 via 192.0.2.3 dev $swp1
	ip nexthop add id 10 group 1/2
	check_fail $? "managed to configure a nexthop group with device-only nexthops when should not"

	ip nexthop add id 10 group 3 type resilient buckets 7
	check_fail $? "managed to configure a too small resilient nexthop group when should not"

	ip nexthop add id 10 group 3 type resilient buckets 129
	check_fail $? "managed to configure a resilient nexthop group with invalid number of buckets when should not"

	ip nexthop add id 10 group 1/2 type resilient buckets 32
	check_fail $? "managed to configure a resilient nexthop group with device-only nexthops when should not"

	ip nexthop add id 10 group 3 type resilient buckets 32
	check_err $? "failed to configure a valid resilient nexthop group"
	ip nexthop replace id 3 dev $swp1
	check_fail $? "managed to populate a nexthop bucket with a device-only nexthop when should not"

	log_test "nexthop objects - invalid configurations"

	ip nexthop del id 10
	ip nexthop del id 3
	ip nexthop del id 2
	ip nexthop del id 1

	simple_if_fini $swp2 192.0.2.2/24 2001:db8:1::2/64
	simple_if_fini $swp1 192.0.2.1/24 2001:db8:1::1/64
}

nexthop_obj_offload_test()
{
	# Test offload indication of nexthop objects
	RET=0

	simple_if_init $swp1 192.0.2.1/24 2001:db8:1::1/64
	simple_if_init $swp2
	setup_wait

	ip nexthop add id 1 via 192.0.2.2 dev $swp1
	ip neigh replace 192.0.2.2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1

	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 1
	check_err $? "nexthop not marked as offloaded when should"

	ip neigh replace 192.0.2.2 nud failed dev $swp1
	busywait "$TIMEOUT" not wait_for_offload \
		ip nexthop show id 1
	check_err $? "nexthop marked as offloaded after setting neigh to failed state"

	ip neigh replace 192.0.2.2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 1
	check_err $? "nexthop not marked as offloaded after neigh replace"

	ip nexthop replace id 1 via 192.0.2.3 dev $swp1
	busywait "$TIMEOUT" not wait_for_offload \
		ip nexthop show id 1
	check_err $? "nexthop marked as offloaded after replacing to use an invalid address"

	ip nexthop replace id 1 via 192.0.2.2 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 1
	check_err $? "nexthop not marked as offloaded after replacing to use a valid address"

	log_test "nexthop objects offload indication"

	ip neigh del 192.0.2.2 dev $swp1
	ip nexthop del id 1

	simple_if_fini $swp2
	simple_if_fini $swp1 192.0.2.1/24 2001:db8:1::1/64
}

nexthop_obj_group_offload_test()
{
	# Test offload indication of nexthop group objects
	RET=0

	simple_if_init $swp1 192.0.2.1/24 2001:db8:1::1/64
	simple_if_init $swp2
	setup_wait

	ip nexthop add id 1 via 192.0.2.2 dev $swp1
	ip nexthop add id 2 via 2001:db8:1::2 dev $swp1
	ip nexthop add id 10 group 1/2
	ip neigh replace 192.0.2.2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	ip neigh replace 192.0.2.3 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	ip neigh replace 2001:db8:1::2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1

	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 1
	check_err $? "IPv4 nexthop not marked as offloaded when should"
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 2
	check_err $? "IPv6 nexthop not marked as offloaded when should"
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 10
	check_err $? "nexthop group not marked as offloaded when should"

	# Invalidate nexthop id 1
	ip neigh replace 192.0.2.2 nud failed dev $swp1
	busywait "$TIMEOUT" not wait_for_offload \
		ip nexthop show id 10
	check_fail $? "nexthop group not marked as offloaded with one valid nexthop"

	# Invalidate nexthop id 2
	ip neigh replace 2001:db8:1::2 nud failed dev $swp1
	busywait "$TIMEOUT" not wait_for_offload \
		ip nexthop show id 10
	check_err $? "nexthop group marked as offloaded when should not"

	# Revalidate nexthop id 1
	ip nexthop replace id 1 via 192.0.2.3 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 10
	check_err $? "nexthop group not marked as offloaded after revalidating nexthop"

	log_test "nexthop group objects offload indication"

	ip neigh del 2001:db8:1::2 dev $swp1
	ip neigh del 192.0.2.3 dev $swp1
	ip neigh del 192.0.2.2 dev $swp1
	ip nexthop del id 10
	ip nexthop del id 2
	ip nexthop del id 1

	simple_if_fini $swp2
	simple_if_fini $swp1 192.0.2.1/24 2001:db8:1::1/64
}

nexthop_obj_bucket_offload_test()
{
	# Test offload indication of nexthop buckets
	RET=0

	simple_if_init $swp1 192.0.2.1/24 2001:db8:1::1/64
	simple_if_init $swp2
	setup_wait

	ip nexthop add id 1 via 192.0.2.2 dev $swp1
	ip nexthop add id 2 via 2001:db8:1::2 dev $swp1
	ip nexthop add id 10 group 1/2 type resilient buckets 32 idle_timer 0
	ip neigh replace 192.0.2.2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	ip neigh replace 192.0.2.3 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	ip neigh replace 2001:db8:1::2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1

	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop bucket show nhid 1
	check_err $? "IPv4 nexthop buckets not marked as offloaded when should"
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop bucket show nhid 2
	check_err $? "IPv6 nexthop buckets not marked as offloaded when should"

	# Invalidate nexthop id 1
	ip neigh replace 192.0.2.2 nud failed dev $swp1
	busywait "$TIMEOUT" wait_for_trap \
		ip nexthop bucket show nhid 1
	check_err $? "IPv4 nexthop buckets not marked with trap when should"

	# Invalidate nexthop id 2
	ip neigh replace 2001:db8:1::2 nud failed dev $swp1
	busywait "$TIMEOUT" wait_for_trap \
		ip nexthop bucket show nhid 2
	check_err $? "IPv6 nexthop buckets not marked with trap when should"

	# Revalidate nexthop id 1 by changing its configuration
	ip nexthop replace id 1 via 192.0.2.3 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop bucket show nhid 1
	check_err $? "nexthop bucket not marked as offloaded after revalidating nexthop"

	# Revalidate nexthop id 2 by changing its neighbour
	ip neigh replace 2001:db8:1::2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop bucket show nhid 2
	check_err $? "nexthop bucket not marked as offloaded after revalidating neighbour"

	log_test "nexthop bucket offload indication"

	ip neigh del 2001:db8:1::2 dev $swp1
	ip neigh del 192.0.2.3 dev $swp1
	ip neigh del 192.0.2.2 dev $swp1
	ip nexthop del id 10
	ip nexthop del id 2
	ip nexthop del id 1

	simple_if_fini $swp2
	simple_if_fini $swp1 192.0.2.1/24 2001:db8:1::1/64
}

nexthop_obj_blackhole_offload_test()
{
	# Test offload indication of blackhole nexthop objects
	RET=0

	ip nexthop add id 1 blackhole
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 1
	check_err $? "Blackhole nexthop not marked as offloaded when should"

	ip nexthop add id 10 group 1
	busywait "$TIMEOUT" wait_for_offload \
		ip nexthop show id 10
	check_err $? "Nexthop group not marked as offloaded when should"

	log_test "blackhole nexthop objects offload indication"

	ip nexthop del id 10
	ip nexthop del id 1
}

nexthop_obj_route_offload_test()
{
	# Test offload indication of routes using nexthop objects
	RET=0

	simple_if_init $swp1 192.0.2.1/24 2001:db8:1::1/64
	simple_if_init $swp2
	setup_wait

	ip nexthop add id 1 via 192.0.2.2 dev $swp1
	ip neigh replace 192.0.2.2 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1
	ip neigh replace 192.0.2.3 lladdr 00:11:22:33:44:55 nud perm \
		dev $swp1

	ip route replace 198.51.100.0/24 nhid 1
	busywait "$TIMEOUT" wait_for_offload \
		ip route show 198.51.100.0/24
	check_err $? "route not marked as offloaded when using valid nexthop"

	ip nexthop replace id 1 via 192.0.2.3 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip route show 198.51.100.0/24
	check_err $? "route not marked as offloaded after replacing valid nexthop with a valid one"

	ip nexthop replace id 1 via 192.0.2.4 dev $swp1
	busywait "$TIMEOUT" not wait_for_offload \
		ip route show 198.51.100.0/24
	check_err $? "route marked as offloaded after replacing valid nexthop with an invalid one"

	ip nexthop replace id 1 via 192.0.2.2 dev $swp1
	busywait "$TIMEOUT" wait_for_offload \
		ip route show 198.51.100.0/24
	check_err $? "route not marked as offloaded after replacing invalid nexthop with a valid one"

	log_test "routes using nexthop objects offload indication"

	ip route del 198.51.100.0/24
	ip neigh del 192.0.2.3 dev $swp1
	ip neigh del 192.0.2.2 dev $swp1
	ip nexthop del id 1

	simple_if_fini $swp2
	simple_if_fini $swp1 192.0.2.1/24 2001:db8:1::1/64
}

bridge_locked_port_test()
{
	RET=0

	ip link add name br1 up type bridge vlan_filtering 0

	ip link add link $swp1 name $swp1.10 type vlan id 10
	ip link set dev $swp1.10 master br1

	bridge link set dev $swp1.10 locked on
	check_fail $? "managed to set locked flag on a VLAN upper"

	ip link set dev $swp1.10 nomaster
	ip link set dev $swp1 master br1

	bridge link set dev $swp1 locked on
	check_fail $? "managed to set locked flag on a bridge port that has a VLAN upper"

	ip link del dev $swp1.10
	bridge link set dev $swp1 locked on

	ip link add link $swp1 name $swp1.10 type vlan id 10
	check_fail $? "managed to configure a VLAN upper on a locked port"

	log_test "bridge locked port"

	ip link del dev $swp1.10 &> /dev/null
	ip link del dev br1
}

devlink_reload_test()
{
	# Test that after executing all the above configuration tests, a
	# devlink reload can be performed without errors
	RET=0

	devlink dev reload "$DEVLINK_DEV"
	check_err $? "devlink reload failed"

	log_test "devlink reload - last test"

	sleep 20
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

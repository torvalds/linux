#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

readonly NETNS="ns-$(mktemp -u XXXXXX)"

ALL_TESTS="
	test_vlan_filter_check
	test_vlan0_del_crash_01
	test_vlan0_del_crash_02
	test_vlan0_del_crash_03
	test_vid0_memleak
"

ret=0

setup() {
	ip netns add ${NETNS}
}

cleanup() {
	ip netns del $NETNS 2>/dev/null
}

trap cleanup EXIT

fail() {
	echo "ERROR: ${1:-unexpected return code} (ret: $_)" >&2
	ret=1
}

tests_run()
{
	local current_test
	for current_test in ${TESTS:-$ALL_TESTS}; do
		$current_test
	done
}

test_vlan_filter_check() {
	setup
	ip netns exec ${NETNS} ip link add bond0 type bond mode 0
	ip netns exec ${NETNS} ip link add bond_slave_1 type veth peer veth2
	ip netns exec ${NETNS} ip link set bond_slave_1 master bond0
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter off
	ip netns exec ${NETNS} ip link add link bond_slave_1 name bond_slave_1.0 type vlan id 0
	ip netns exec ${NETNS} ip link add link bond0 name bond0.0 type vlan id 0
	ip netns exec ${NETNS} ip link set bond_slave_1 nomaster
	ip netns exec ${NETNS} ip link del veth2 || fail "Please check vlan HW filter function"
	cleanup
}

#enable vlan_filter feature of real_dev with vlan0 during running time
test_vlan0_del_crash_01() {
	setup
	ip netns exec ${NETNS} ip link add bond0 type bond mode 0
	ip netns exec ${NETNS} ip link add link bond0 name vlan0 type vlan id 0 protocol 802.1q
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter off
	ip netns exec ${NETNS} ip link set dev bond0 up
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter on
	ip netns exec ${NETNS} ip link set dev bond0 down
	ip netns exec ${NETNS} ip link set dev bond0 up
	ip netns exec ${NETNS} ip link del vlan0 || fail "Please check vlan HW filter function"
	cleanup
}

#enable vlan_filter feature and add vlan0 for real_dev during running time
test_vlan0_del_crash_02() {
	setup
	ip netns exec ${NETNS} ip link add bond0 type bond mode 0
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter off
	ip netns exec ${NETNS} ip link set dev bond0 up
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter on
	ip netns exec ${NETNS} ip link add link bond0 name vlan0 type vlan id 0 protocol 802.1q
	ip netns exec ${NETNS} ip link set dev bond0 down
	ip netns exec ${NETNS} ip link set dev bond0 up
	ip netns exec ${NETNS} ip link del vlan0 || fail "Please check vlan HW filter function"
	cleanup
}

#enable vlan_filter feature of real_dev during running time
#test kernel_bug of vlan unregister
test_vlan0_del_crash_03() {
	setup
	ip netns exec ${NETNS} ip link add bond0 type bond mode 0
	ip netns exec ${NETNS} ip link add link bond0 name vlan0 type vlan id 0 protocol 802.1q
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter off
	ip netns exec ${NETNS} ip link set dev bond0 up
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter on
	ip netns exec ${NETNS} ip link set dev bond0 down
	ip netns exec ${NETNS} ip link del vlan0 || fail "Please check vlan HW filter function"
	cleanup
}

test_vid0_memleak() {
	setup
	ip netns exec ${NETNS} ip link add bond0 up type bond mode 0
	ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter off
	ip netns exec ${NETNS} ip link del dev bond0 || fail "Please check vlan HW filter function"
	cleanup
}

tests_run
exit $ret

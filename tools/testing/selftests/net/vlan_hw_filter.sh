#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

readonly NETNS="ns-$(mktemp -u XXXXXX)"

ret=0

cleanup() {
	ip netns del $NETNS
}

trap cleanup EXIT

fail() {
    echo "ERROR: ${1:-unexpected return code} (ret: $_)" >&2
    ret=1
}

ip netns add ${NETNS}
ip netns exec ${NETNS} ip link add bond0 type bond mode 0
ip netns exec ${NETNS} ip link add bond_slave_1 type veth peer veth2
ip netns exec ${NETNS} ip link set bond_slave_1 master bond0
ip netns exec ${NETNS} ethtool -K bond0 rx-vlan-filter off
ip netns exec ${NETNS} ip link add link bond_slave_1 name bond_slave_1.0 type vlan id 0
ip netns exec ${NETNS} ip link add link bond0 name bond0.0 type vlan id 0
ip netns exec ${NETNS} ip link set bond_slave_1 nomaster
ip netns exec ${NETNS} ip link del veth2 || fail "Please check vlan HW filter function"

exit $ret

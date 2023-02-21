#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

NAMESPACES=""

# Test that a link aggregation device (bonding, team) removes the hardware
# addresses that it adds on its underlying devices.
test_LAG_cleanup()
{
	local driver=$1
	local mode=$2
	local ucaddr="02:00:00:12:34:56"
	local addr6="fe80::78:9abc/64"
	local mcaddr="33:33:ff:78:9a:bc"
	local name

	ip link add dummy1 type dummy
	ip link add dummy2 type dummy
	if [ "$driver" = "bonding" ]; then
		name="bond1"
		ip link add "$name" up type bond mode "$mode"
		ip link set dev dummy1 master "$name"
		ip link set dev dummy2 master "$name"
	elif [ "$driver" = "team" ]; then
		name="team0"
		teamd -d -c '
			{
				"device": "'"$name"'",
				"runner": {
					"name": "'"$mode"'"
				},
				"ports": {
					"dummy1":
						{},
					"dummy2":
						{}
				}
			}
		'
		ip link set dev "$name" up
	else
		check_err 1
		log_test test_LAG_cleanup ": unknown driver \"$driver\""
		return
	fi

	# Used to test dev->uc handling
	ip link add mv0 link "$name" up address "$ucaddr" type macvlan
	# Used to test dev->mc handling
	ip address add "$addr6" dev "$name"
	ip link set dev "$name" down
	ip link del "$name"

	not grep_bridge_fdb "$ucaddr" bridge fdb show >/dev/null
	check_err $? "macvlan unicast address still present on a slave"

	not grep_bridge_fdb "$mcaddr" bridge fdb show >/dev/null
	check_err $? "IPv6 solicited-node multicast mac address still present on a slave"

	cleanup

	log_test "$driver cleanup mode $mode"
}

# Build a generic 2 node net namespace with 2 connections
# between the namespaces
#
#  +-----------+       +-----------+
#  | node1     |       | node2     |
#  |           |       |           |
#  |           |       |           |
#  |      eth0 +-------+ eth0      |
#  |           |       |           |
#  |      eth1 +-------+ eth1      |
#  |           |       |           |
#  +-----------+       +-----------+
lag_setup2x2()
{
	local state=${1:-down}
	local namespaces="lag_node1 lag_node2"

	# create namespaces
	for n in ${namespaces}; do
		ip netns add ${n}
	done

	# wire up namespaces
	ip link add name lag1 type veth peer name lag1-end
	ip link set dev lag1 netns lag_node1 $state name eth0
	ip link set dev lag1-end netns lag_node2 $state name eth0

	ip link add name lag1 type veth peer name lag1-end
	ip link set dev lag1 netns lag_node1 $state name eth1
	ip link set dev lag1-end netns lag_node2 $state name eth1

	NAMESPACES="${namespaces}"
}

# cleanup all lag related namespaces and remove the bonding module
lag_cleanup()
{
	for n in ${NAMESPACES}; do
		ip netns delete ${n} >/dev/null 2>&1 || true
	done
	modprobe -r bonding
}

SWITCH="lag_node1"
CLIENT="lag_node2"
CLIENTIP="172.20.2.1"
SWITCHIP="172.20.2.2"

lag_setup_network()
{
	lag_setup2x2 "down"

	# create switch
	ip netns exec ${SWITCH} ip link add br0 up type bridge
	ip netns exec ${SWITCH} ip link set eth0 master br0 up
	ip netns exec ${SWITCH} ip link set eth1 master br0 up
	ip netns exec ${SWITCH} ip addr add ${SWITCHIP}/24 dev br0
}

lag_reset_network()
{
	ip netns exec ${CLIENT} ip link del bond0
	ip netns exec ${SWITCH} ip link set eth0 up
	ip netns exec ${SWITCH} ip link set eth1 up
}

create_bond()
{
	# create client
	ip netns exec ${CLIENT} ip link set eth0 down
	ip netns exec ${CLIENT} ip link set eth1 down

	ip netns exec ${CLIENT} ip link add bond0 type bond $@
	ip netns exec ${CLIENT} ip link set eth0 master bond0
	ip netns exec ${CLIENT} ip link set eth1 master bond0
	ip netns exec ${CLIENT} ip link set bond0 up
	ip netns exec ${CLIENT} ip addr add ${CLIENTIP}/24 dev bond0
}

test_bond_recovery()
{
	RET=0

	create_bond $@

	# verify connectivity
	ip netns exec ${CLIENT} ping ${SWITCHIP} -c 2 >/dev/null 2>&1
	check_err $? "No connectivity"

	# force the links of the bond down
	ip netns exec ${SWITCH} ip link set eth0 down
	sleep 2
	ip netns exec ${SWITCH} ip link set eth0 up
	ip netns exec ${SWITCH} ip link set eth1 down

	# re-verify connectivity
	ip netns exec ${CLIENT} ping ${SWITCHIP} -c 2 >/dev/null 2>&1

	local rc=$?
	check_err $rc "Bond failed to recover"
	log_test "$1 ($2) bond recovery"
	lag_reset_network
}

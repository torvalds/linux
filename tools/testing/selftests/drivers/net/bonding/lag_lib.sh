#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

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

	# Check that addresses were added as expected
	(grep_bridge_fdb "$ucaddr" bridge fdb show dev dummy1 ||
		grep_bridge_fdb "$ucaddr" bridge fdb show dev dummy2) >/dev/null
	check_err $? "macvlan unicast address not found on a slave"

	# mcaddr is added asynchronously by addrconf_dad_work(), use busywait
	(busywait 10000 grep_bridge_fdb "$mcaddr" bridge fdb show dev dummy1 ||
		grep_bridge_fdb "$mcaddr" bridge fdb show dev dummy2) >/dev/null
	check_err $? "IPv6 solicited-node multicast mac address not found on a slave"

	ip link set dev "$name" down
	ip link del "$name"

	not grep_bridge_fdb "$ucaddr" bridge fdb show >/dev/null
	check_err $? "macvlan unicast address still present on a slave"

	not grep_bridge_fdb "$mcaddr" bridge fdb show >/dev/null
	check_err $? "IPv6 solicited-node multicast mac address still present on a slave"

	cleanup

	log_test "$driver cleanup mode $mode"
}

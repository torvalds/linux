#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bond device handling of addr lists (dev->uc, mc)
#

ALL_TESTS="
	bond_cleanup_mode1
	bond_cleanup_mode4
	bond_listen_lacpdu_multicast_case_down
	bond_listen_lacpdu_multicast_case_up
"

REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/net_forwarding_lib.sh

source "$lib_dir"/lag_lib.sh


destroy()
{
	local ifnames=(dummy1 dummy2 bond1 mv0)
	local ifname

	for ifname in "${ifnames[@]}"; do
		ip link del "$ifname" &>/dev/null
	done
}

cleanup()
{
	pre_cleanup

	destroy
}


# bond driver control paths vary between modes that have a primary slave
# (bond_uses_primary()) and others. Test both kinds of modes.

bond_cleanup_mode1()
{
	RET=0

	test_LAG_cleanup "bonding" "active-backup"
}

bond_cleanup_mode4() {
	RET=0

	test_LAG_cleanup "bonding" "802.3ad"
}

bond_listen_lacpdu_multicast()
{
	# Initial state of bond device, up | down
	local init_state=$1
	local lacpdu_mc="01:80:c2:00:00:02"

	ip link add dummy1 type dummy
	ip link add bond1 "$init_state" type bond mode 802.3ad
	ip link set dev dummy1 master bond1
	if [ "$init_state" = "down" ]; then
		ip link set dev bond1 up
	fi

	grep_bridge_fdb "$lacpdu_mc" bridge fdb show brport dummy1 >/dev/null
	check_err $? "LACPDU multicast address not present on slave (1)"

	ip link set dev bond1 down

	not grep_bridge_fdb "$lacpdu_mc" bridge fdb show brport dummy1 >/dev/null
	check_err $? "LACPDU multicast address still present on slave"

	ip link set dev bond1 up

	grep_bridge_fdb "$lacpdu_mc" bridge fdb show brport dummy1 >/dev/null
	check_err $? "LACPDU multicast address not present on slave (2)"

	cleanup

	log_test "bonding LACPDU multicast address to slave (from bond $init_state)"
}

# The LACPDU mc addr is added by different paths depending on the initial state
# of the bond when enslaving a device. Test both cases.

bond_listen_lacpdu_multicast_case_down()
{
	RET=0

	bond_listen_lacpdu_multicast "down"
}

bond_listen_lacpdu_multicast_case_up()
{
	RET=0

	bond_listen_lacpdu_multicast "up"
}


trap cleanup EXIT

tests_run

exit "$EXIT_STATUS"

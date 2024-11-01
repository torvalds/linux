#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test team device handling of addr lists (dev->uc, mc)
#

ALL_TESTS="
	team_cleanup
"

REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/net_forwarding_lib.sh

source "$lib_dir"/lag_lib.sh


destroy()
{
	local ifnames=(dummy1 dummy2 team0 mv0)
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


team_cleanup()
{
	RET=0

	test_LAG_cleanup "team" "lacp"
}


require_command teamd

trap cleanup EXIT

tests_run

exit "$EXIT_STATUS"

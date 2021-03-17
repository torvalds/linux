#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	autoneg
	autoneg_force_mode
	no_cable
"

NUM_NETIFS=2
source lib.sh
source ethtool_lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}
	swp3=$NETIF_NO_CABLE
}

ethtool_extended_state_check()
{
	local dev=$1; shift
	local expected_ext_state=$1; shift
	local expected_ext_substate=${1:-""}; shift

	local ext_state=$(ethtool $dev | grep "Link detected" \
		| cut -d "(" -f2 | cut -d ")" -f1)
	local ext_substate=$(echo $ext_state | cut -sd "," -f2 \
		| sed -e 's/^[[:space:]]*//')
	ext_state=$(echo $ext_state | cut -d "," -f1)

	[[ $ext_state == $expected_ext_state ]]
	check_err $? "Expected \"$expected_ext_state\", got \"$ext_state\""

	[[ $ext_substate == $expected_ext_substate ]]
	check_err $? "Expected \"$expected_ext_substate\", got \"$ext_substate\""
}

autoneg()
{
	RET=0

	ip link set dev $swp1 up

	sleep 4
	ethtool_extended_state_check $swp1 "Autoneg" "No partner detected"

	log_test "Autoneg, No partner detected"

	ip link set dev $swp1 down
}

autoneg_force_mode()
{
	RET=0

	ip link set dev $swp1 up
	ip link set dev $swp2 up

	local -a speeds_arr=($(different_speeds_get $swp1 $swp2 0 0))
	local speed1=${speeds_arr[0]}
	local speed2=${speeds_arr[1]}

	ethtool_set $swp1 speed $speed1 autoneg off
	ethtool_set $swp2 speed $speed2 autoneg off

	sleep 4
	ethtool_extended_state_check $swp1 "Autoneg" \
		"No partner detected during force mode"

	ethtool_extended_state_check $swp2 "Autoneg" \
		"No partner detected during force mode"

	log_test "Autoneg, No partner detected during force mode"

	ethtool -s $swp2 autoneg on
	ethtool -s $swp1 autoneg on

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

no_cable()
{
	RET=0

	ip link set dev $swp3 up

	sleep 1
	ethtool_extended_state_check $swp3 "No cable"

	log_test "No cable"

	ip link set dev $swp3 down
}

setup_prepare

tests_run

exit $EXIT_STATUS

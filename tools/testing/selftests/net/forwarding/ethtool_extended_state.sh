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

TIMEOUT=$((WAIT_TIMEOUT * 1000)) # ms

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}
	swp3=$NETIF_NO_CABLE
}

ethtool_ext_state()
{
	local dev=$1; shift
	local expected_ext_state=$1; shift
	local expected_ext_substate=${1:-""}; shift

	local ext_state=$(ethtool $dev | grep "Link detected" \
		| cut -d "(" -f2 | cut -d ")" -f1)
	local ext_substate=$(echo $ext_state | cut -sd "," -f2 \
		| sed -e 's/^[[:space:]]*//')
	ext_state=$(echo $ext_state | cut -d "," -f1)

	if [[ $ext_state != $expected_ext_state ]]; then
		echo "Expected \"$expected_ext_state\", got \"$ext_state\""
		return 1
	fi
	if [[ $ext_substate != $expected_ext_substate ]]; then
		echo "Expected \"$expected_ext_substate\", got \"$ext_substate\""
		return 1
	fi
}

autoneg()
{
	local msg

	RET=0

	ip link set dev $swp1 up

	msg=$(busywait $TIMEOUT ethtool_ext_state $swp1 \
			"Autoneg" "No partner detected")
	check_err $? "$msg"

	log_test "Autoneg, No partner detected"

	ip link set dev $swp1 down
}

autoneg_force_mode()
{
	local msg

	RET=0

	ip link set dev $swp1 up
	ip link set dev $swp2 up

	local -a speeds_arr=($(different_speeds_get $swp1 $swp2 0 0))
	local speed1=${speeds_arr[0]}
	local speed2=${speeds_arr[1]}

	ethtool_set $swp1 speed $speed1 autoneg off
	ethtool_set $swp2 speed $speed2 autoneg off

	msg=$(busywait $TIMEOUT ethtool_ext_state $swp1 \
			"Autoneg" "No partner detected during force mode")
	check_err $? "$msg"

	msg=$(busywait $TIMEOUT ethtool_ext_state $swp2 \
			"Autoneg" "No partner detected during force mode")
	check_err $? "$msg"

	log_test "Autoneg, No partner detected during force mode"

	ethtool -s $swp2 autoneg on
	ethtool -s $swp1 autoneg on

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

no_cable()
{
	local msg

	RET=0

	ip link set dev $swp3 up

	msg=$(busywait $TIMEOUT ethtool_ext_state $swp3 "No cable")
	check_err $? "$msg"

	log_test "No cable"

	ip link set dev $swp3 down
}

setup_prepare

tests_run

exit $EXIT_STATUS

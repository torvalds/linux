#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	autoneg
	autoneg_force_mode
"

NUM_NETIFS=2
: ${TIMEOUT:=30000} # ms
source $lib_dir/lib.sh
source $lib_dir/ethtool_lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	ip link set dev $swp1 up
	ip link set dev $swp2 up

	busywait "$TIMEOUT" wait_for_port_up ethtool $swp2
	check_err $? "ports did not come up"

	local lanes_exist=$(ethtool $swp1 | grep 'Lanes:')
	if [[ -z $lanes_exist ]]; then
		log_test "SKIP: driver does not support lanes setting"
		exit 1
	fi

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

check_lanes()
{
	local dev=$1; shift
	local lanes=$1; shift
	local max_speed=$1; shift
	local chosen_lanes

	chosen_lanes=$(ethtool $dev | grep 'Lanes:')
	chosen_lanes=${chosen_lanes#*"Lanes: "}

	((chosen_lanes == lanes))
	check_err $? "swp1 advertise $max_speed and $lanes, devs sync to $chosen_lanes"
}

check_unsupported_lanes()
{
	local dev=$1; shift
	local max_speed=$1; shift
	local max_lanes=$1; shift
	local autoneg=$1; shift
	local autoneg_str=""

	local unsupported_lanes=$((max_lanes *= 2))

	if [[ $autoneg -eq 0 ]]; then
		autoneg_str="autoneg off"
	fi

	ethtool -s $swp1 speed $max_speed lanes $unsupported_lanes $autoneg_str &> /dev/null
	check_fail $? "Unsuccessful $unsupported_lanes lanes setting was expected"
}

max_speed_and_lanes_get()
{
	local dev=$1; shift
	local arr=("$@")
	local max_lanes
	local max_speed
	local -a lanes_arr
	local -a speeds_arr
	local -a max_values

	for ((i=0; i<${#arr[@]}; i+=2)); do
		speeds_arr+=("${arr[$i]}")
		lanes_arr+=("${arr[i+1]}")
	done

	max_values+=($(get_max "${speeds_arr[@]}"))
	max_values+=($(get_max "${lanes_arr[@]}"))

	echo ${max_values[@]}
}

search_linkmode()
{
	local speed=$1; shift
	local lanes=$1; shift
	local arr=("$@")

	for ((i=0; i<${#arr[@]}; i+=2)); do
		if [[ $speed -eq ${arr[$i]} && $lanes -eq ${arr[i+1]} ]]; then
			return 1
		fi
	done
	return 0
}

autoneg()
{
	RET=0

	local lanes
	local max_speed
	local max_lanes

	local -a linkmodes_params=($(dev_linkmodes_params_get $swp1 1))
	local -a max_values=($(max_speed_and_lanes_get $swp1 "${linkmodes_params[@]}"))
	max_speed=${max_values[0]}
	max_lanes=${max_values[1]}

	lanes=$max_lanes

	while [[ $lanes -ge 1 ]]; do
		search_linkmode $max_speed $lanes "${linkmodes_params[@]}"
		if [[ $? -eq 1 ]]; then
			ethtool_set $swp1 speed $max_speed lanes $lanes
			ip link set dev $swp1 up
			ip link set dev $swp2 up
			busywait "$TIMEOUT" wait_for_port_up ethtool $swp2
			check_err $? "ports did not come up"

			check_lanes $swp1 $lanes $max_speed
			log_test "$lanes lanes is autonegotiated"
		fi
		let $((lanes /= 2))
	done

	check_unsupported_lanes $swp1 $max_speed $max_lanes 1
	log_test "Lanes number larger than max width is not set"

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

autoneg_force_mode()
{
	RET=0

	local lanes
	local max_speed
	local max_lanes

	local -a linkmodes_params=($(dev_linkmodes_params_get $swp1 1))
	local -a max_values=($(max_speed_and_lanes_get $swp1 "${linkmodes_params[@]}"))
	max_speed=${max_values[0]}
	max_lanes=${max_values[1]}

	lanes=$max_lanes

	while [[ $lanes -ge 1 ]]; do
		search_linkmode $max_speed $lanes "${linkmodes_params[@]}"
		if [[ $? -eq 1 ]]; then
			ethtool_set $swp1 speed $max_speed lanes $lanes autoneg off
			ethtool_set $swp2 speed $max_speed lanes $lanes autoneg off
			ip link set dev $swp1 up
			ip link set dev $swp2 up
			busywait "$TIMEOUT" wait_for_port_up ethtool $swp2
			check_err $? "ports did not come up"

			check_lanes $swp1 $lanes $max_speed
			log_test "Autoneg off, $lanes lanes detected during force mode"
		fi
		let $((lanes /= 2))
	done

	check_unsupported_lanes $swp1 $max_speed $max_lanes 0
	log_test "Lanes number larger than max width is not set"

	ip link set dev $swp2 down
	ip link set dev $swp1 down

	ethtool -s $swp2 autoneg on
	ethtool -s $swp1 autoneg on
}

check_ethtool_lanes_support
setup_prepare

tests_run

exit $EXIT_STATUS

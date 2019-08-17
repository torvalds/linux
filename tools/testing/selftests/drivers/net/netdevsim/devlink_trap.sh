#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking devlink-trap functionality. It makes use of
# netdevsim which implements the required callbacks.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	init_test
	trap_action_test
	trap_metadata_test
	bad_trap_test
	bad_trap_action_test
	trap_stats_test
	trap_group_action_test
	bad_trap_group_test
	trap_group_stats_test
	port_del_test
	dev_del_test
"
NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
DEVLINK_DEV=netdevsim/${DEV}
SLEEP_TIME=1
NETDEV=""
NUM_NETIFS=0
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

require_command udevadm

modprobe netdevsim &> /dev/null
if [ ! -d "$NETDEVSIM_PATH" ]; then
	echo "SKIP: No netdevsim support"
	exit 1
fi

if [ -d "${NETDEVSIM_PATH}/devices/netdevsim${DEV_ADDR}" ]; then
	echo "SKIP: Device netdevsim${DEV_ADDR} already exists"
	exit 1
fi

init_test()
{
	RET=0

	test $(devlink_traps_num_get) -ne 0
	check_err $? "No traps were registered"

	log_test "Initialization"
}

trap_action_test()
{
	local orig_action
	local trap_name
	local action

	RET=0

	for trap_name in $(devlink_traps_get); do
		# The action of non-drop traps cannot be changed.
		if [ $(devlink_trap_type_get $trap_name) = "drop" ]; then
			devlink_trap_action_set $trap_name "trap"
			action=$(devlink_trap_action_get $trap_name)
			if [ $action != "trap" ]; then
				check_err 1 "Trap $trap_name did not change action to trap"
			fi

			devlink_trap_action_set $trap_name "drop"
			action=$(devlink_trap_action_get $trap_name)
			if [ $action != "drop" ]; then
				check_err 1 "Trap $trap_name did not change action to drop"
			fi
		else
			orig_action=$(devlink_trap_action_get $trap_name)

			devlink_trap_action_set $trap_name "trap"
			action=$(devlink_trap_action_get $trap_name)
			if [ $action != $orig_action ]; then
				check_err 1 "Trap $trap_name changed action when should not"
			fi

			devlink_trap_action_set $trap_name "drop"
			action=$(devlink_trap_action_get $trap_name)
			if [ $action != $orig_action ]; then
				check_err 1 "Trap $trap_name changed action when should not"
			fi
		fi
	done

	log_test "Trap action"
}

trap_metadata_test()
{
	local trap_name

	RET=0

	for trap_name in $(devlink_traps_get); do
		devlink_trap_metadata_test $trap_name "input_port"
		check_err $? "Input port not reported as metadata of trap $trap_name"
	done

	log_test "Trap metadata"
}

bad_trap_test()
{
	RET=0

	devlink_trap_action_set "made_up_trap" "drop"
	check_fail $? "Did not get an error for non-existing trap"

	log_test "Non-existing trap"
}

bad_trap_action_test()
{
	local traps_arr
	local trap_name

	RET=0

	# Pick first trap.
	traps_arr=($(devlink_traps_get))
	trap_name=${traps_arr[0]}

	devlink_trap_action_set $trap_name "made_up_action"
	check_fail $? "Did not get an error for non-existing trap action"

	log_test "Non-existing trap action"
}

trap_stats_test()
{
	local trap_name

	RET=0

	for trap_name in $(devlink_traps_get); do
		devlink_trap_stats_idle_test $trap_name
		check_err $? "Stats of trap $trap_name not idle when netdev down"

		ip link set dev $NETDEV up

		if [ $(devlink_trap_type_get $trap_name) = "drop" ]; then
			devlink_trap_action_set $trap_name "trap"
			devlink_trap_stats_idle_test $trap_name
			check_fail $? "Stats of trap $trap_name idle when action is trap"

			devlink_trap_action_set $trap_name "drop"
			devlink_trap_stats_idle_test $trap_name
			check_err $? "Stats of trap $trap_name not idle when action is drop"
		else
			devlink_trap_stats_idle_test $trap_name
			check_fail $? "Stats of non-drop trap $trap_name idle when should not"
		fi

		ip link set dev $NETDEV down
	done

	log_test "Trap statistics"
}

trap_group_action_test()
{
	local curr_group group_name
	local trap_name
	local trap_type
	local action

	RET=0

	for group_name in $(devlink_trap_groups_get); do
		devlink_trap_group_action_set $group_name "trap"

		for trap_name in $(devlink_traps_get); do
			curr_group=$(devlink_trap_group_get $trap_name)
			if [ $curr_group != $group_name ]; then
				continue
			fi

			trap_type=$(devlink_trap_type_get $trap_name)
			if [ $trap_type != "drop" ]; then
				continue
			fi

			action=$(devlink_trap_action_get $trap_name)
			if [ $action != "trap" ]; then
				check_err 1 "Trap $trap_name did not change action to trap"
			fi
		done

		devlink_trap_group_action_set $group_name "drop"

		for trap_name in $(devlink_traps_get); do
			curr_group=$(devlink_trap_group_get $trap_name)
			if [ $curr_group != $group_name ]; then
				continue
			fi

			trap_type=$(devlink_trap_type_get $trap_name)
			if [ $trap_type != "drop" ]; then
				continue
			fi

			action=$(devlink_trap_action_get $trap_name)
			if [ $action != "drop" ]; then
				check_err 1 "Trap $trap_name did not change action to drop"
			fi
		done
	done

	log_test "Trap group action"
}

bad_trap_group_test()
{
	RET=0

	devlink_trap_group_action_set "made_up_trap_group" "drop"
	check_fail $? "Did not get an error for non-existing trap group"

	log_test "Non-existing trap group"
}

trap_group_stats_test()
{
	local group_name

	RET=0

	for group_name in $(devlink_trap_groups_get); do
		devlink_trap_group_stats_idle_test $group_name
		check_err $? "Stats of trap group $group_name not idle when netdev down"

		ip link set dev $NETDEV up

		devlink_trap_group_action_set $group_name "trap"
		devlink_trap_group_stats_idle_test $group_name
		check_fail $? "Stats of trap group $group_name idle when action is trap"

		devlink_trap_group_action_set $group_name "drop"
		ip link set dev $NETDEV down
	done

	log_test "Trap group statistics"
}

port_del_test()
{
	local group_name
	local i

	# The test never fails. It is meant to exercise different code paths
	# and make sure we properly dismantle a port while packets are
	# in-flight.
	RET=0

	devlink_traps_enable_all

	for i in $(seq 1 10); do
		ip link set dev $NETDEV up

		sleep $SLEEP_TIME

		netdevsim_port_destroy
		netdevsim_port_create
		udevadm settle
	done

	devlink_traps_disable_all

	log_test "Port delete"
}

dev_del_test()
{
	local group_name
	local i

	# The test never fails. It is meant to exercise different code paths
	# and make sure we properly unregister traps while packets are
	# in-flight.
	RET=0

	devlink_traps_enable_all

	for i in $(seq 1 10); do
		ip link set dev $NETDEV up

		sleep $SLEEP_TIME

		cleanup
		setup_prepare
	done

	devlink_traps_disable_all

	log_test "Device delete"
}

netdevsim_dev_create()
{
	echo "$DEV_ADDR 0" > ${NETDEVSIM_PATH}/new_device
}

netdevsim_dev_destroy()
{
	echo "$DEV_ADDR" > ${NETDEVSIM_PATH}/del_device
}

netdevsim_port_create()
{
	echo 1 > ${NETDEVSIM_PATH}/devices/${DEV}/new_port
}

netdevsim_port_destroy()
{
	echo 1 > ${NETDEVSIM_PATH}/devices/${DEV}/del_port
}

setup_prepare()
{
	local netdev

	netdevsim_dev_create

	if [ ! -d "${NETDEVSIM_PATH}/devices/${DEV}" ]; then
		echo "Failed to create netdevsim device"
		exit 1
	fi

	netdevsim_port_create

	if [ ! -d "${NETDEVSIM_PATH}/devices/${DEV}/net/" ]; then
		echo "Failed to create netdevsim port"
		exit 1
	fi

	# Wait for udev to rename newly created netdev.
	udevadm settle

	NETDEV=$(ls ${NETDEVSIM_PATH}/devices/${DEV}/net/)
}

cleanup()
{
	pre_cleanup
	netdevsim_port_destroy
	netdevsim_dev_destroy
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS

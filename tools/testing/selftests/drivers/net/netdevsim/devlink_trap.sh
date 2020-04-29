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
	trap_policer_test
	trap_policer_bind_test
	port_del_test
	dev_del_test
"
NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
DEVLINK_DEV=netdevsim/${DEV}
DEBUGFS_DIR=/sys/kernel/debug/netdevsim/$DEV/
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
		if [ $trap_name == "ingress_flow_action_drop" ] ||
		   [ $trap_name == "egress_flow_action_drop" ]; then
			devlink_trap_metadata_test $trap_name "flow_action_cookie"
			check_err $? "Flow action cookie not reported as metadata of trap $trap_name"
		fi
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

trap_policer_test()
{
	local packets_t0
	local packets_t1

	if [ $(devlink_trap_policers_num_get) -eq 0 ]; then
		check_err 1 "Failed to dump policers"
	fi

	devlink trap policer set $DEVLINK_DEV policer 1337 &> /dev/null
	check_fail $? "Did not get an error for setting a non-existing policer"
	devlink trap policer show $DEVLINK_DEV policer 1337 &> /dev/null
	check_fail $? "Did not get an error for getting a non-existing policer"

	devlink trap policer set $DEVLINK_DEV policer 1 rate 2000 burst 16
	check_err $? "Failed to set valid parameters for a valid policer"
	if [ $(devlink_trap_policer_rate_get 1) -ne 2000 ]; then
		check_err 1 "Policer rate was not changed"
	fi
	if [ $(devlink_trap_policer_burst_get 1) -ne 16 ]; then
		check_err 1 "Policer burst size was not changed"
	fi

	devlink trap policer set $DEVLINK_DEV policer 1 rate 0 &> /dev/null
	check_fail $? "Policer rate was changed to rate lower than limit"
	devlink trap policer set $DEVLINK_DEV policer 1 rate 9000 &> /dev/null
	check_fail $? "Policer rate was changed to rate higher than limit"
	devlink trap policer set $DEVLINK_DEV policer 1 burst 2 &> /dev/null
	check_fail $? "Policer burst size was changed to burst size lower than limit"
	devlink trap policer set $DEVLINK_DEV policer 1 rate 65537 &> /dev/null
	check_fail $? "Policer burst size was changed to burst size higher than limit"
	echo "y" > $DEBUGFS_DIR/fail_trap_policer_set
	devlink trap policer set $DEVLINK_DEV policer 1 rate 3000 &> /dev/null
	check_fail $? "Managed to set policer rate when should not"
	echo "n" > $DEBUGFS_DIR/fail_trap_policer_set
	if [ $(devlink_trap_policer_rate_get 1) -ne 2000 ]; then
		check_err 1 "Policer rate was changed to an invalid value"
	fi
	if [ $(devlink_trap_policer_burst_get 1) -ne 16 ]; then
		check_err 1 "Policer burst size was changed to an invalid value"
	fi

	packets_t0=$(devlink_trap_policer_rx_dropped_get 1)
	sleep .5
	packets_t1=$(devlink_trap_policer_rx_dropped_get 1)
	if [ ! $packets_t1 -gt $packets_t0 ]; then
		check_err 1 "Policer drop counter was not incremented"
	fi

	echo "y"> $DEBUGFS_DIR/fail_trap_policer_counter_get
	devlink -s trap policer show $DEVLINK_DEV policer 1 &> /dev/null
	check_fail $? "Managed to read policer drop counter when should not"
	echo "n"> $DEBUGFS_DIR/fail_trap_policer_counter_get
	devlink -s trap policer show $DEVLINK_DEV policer 1 &> /dev/null
	check_err $? "Did not manage to read policer drop counter when should"

	log_test "Trap policer"
}

trap_group_check_policer()
{
	local group_name=$1; shift

	devlink -j -p trap group show $DEVLINK_DEV group $group_name \
		| jq -e '.[][][]["policer"]' &> /dev/null
}

trap_policer_bind_test()
{
	devlink trap group set $DEVLINK_DEV group l2_drops policer 1
	check_err $? "Failed to bind a valid policer"
	if [ $(devlink_trap_group_policer_get "l2_drops") -ne 1 ]; then
		check_err 1 "Bound policer was not changed"
	fi

	devlink trap group set $DEVLINK_DEV group l2_drops policer 1337 \
		&> /dev/null
	check_fail $? "Did not get an error for binding a non-existing policer"
	if [ $(devlink_trap_group_policer_get "l2_drops") -ne 1 ]; then
		check_err 1 "Bound policer was changed when should not"
	fi

	devlink trap group set $DEVLINK_DEV group l2_drops policer 0
	check_err $? "Failed to unbind a policer when using ID 0"
	trap_group_check_policer "l2_drops"
	check_fail $? "Trap group has a policer after unbinding with ID 0"

	devlink trap group set $DEVLINK_DEV group l2_drops policer 1
	check_err $? "Failed to bind a valid policer"

	devlink trap group set $DEVLINK_DEV group l2_drops nopolicer
	check_err $? "Failed to unbind a policer when using 'nopolicer' keyword"
	trap_group_check_policer "l2_drops"
	check_fail $? "Trap group has a policer after unbinding with 'nopolicer' keyword"

	devlink trap group set $DEVLINK_DEV group l2_drops policer 1
	check_err $? "Failed to bind a valid policer"

	echo "y"> $DEBUGFS_DIR/fail_trap_group_set
	devlink trap group set $DEVLINK_DEV group l2_drops policer 2 \
		&> /dev/null
	check_fail $? "Managed to bind a policer when should not"
	echo "n"> $DEBUGFS_DIR/fail_trap_group_set
	devlink trap group set $DEVLINK_DEV group l2_drops policer 2
	check_err $? "Did not manage to bind a policer when should"

	devlink trap group set $DEVLINK_DEV group l2_drops action drop \
		policer 1337 &> /dev/null
	check_fail $? "Did not get an error for partially modified trap group"

	log_test "Trap policer binding"
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

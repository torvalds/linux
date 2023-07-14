#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

##############################################################################
# Defines

if [[ ! -v DEVLINK_DEV ]]; then
	DEVLINK_DEV=$(devlink port show "${NETIFS[p1]:-$NETIF_NO_CABLE}" -j \
			     | jq -r '.port | keys[]' | cut -d/ -f-2)
	if [ -z "$DEVLINK_DEV" ]; then
		echo "SKIP: ${NETIFS[p1]} has no devlink device registered for it"
		exit $ksft_skip
	fi
	if [[ "$(echo $DEVLINK_DEV | grep -c pci)" -eq 0 ]]; then
		echo "SKIP: devlink device's bus is not PCI"
		exit $ksft_skip
	fi

	DEVLINK_VIDDID=$(lspci -s $(echo $DEVLINK_DEV | cut -d"/" -f2) \
			 -n | cut -d" " -f3)
elif [[ ! -z "$DEVLINK_DEV" ]]; then
	devlink dev show $DEVLINK_DEV &> /dev/null
	if [ $? -ne 0 ]; then
		echo "SKIP: devlink device \"$DEVLINK_DEV\" not found"
		exit $ksft_skip
	fi
fi

##############################################################################
# Sanity checks

devlink help 2>&1 | grep resource &> /dev/null
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing devlink resource support"
	exit $ksft_skip
fi

devlink help 2>&1 | grep trap &> /dev/null
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing devlink trap support"
	exit $ksft_skip
fi

devlink dev help 2>&1 | grep info &> /dev/null
if [ $? -ne 0 ]; then
	echo "SKIP: iproute2 too old, missing devlink dev info support"
	exit $ksft_skip
fi

##############################################################################
# Devlink helpers

devlink_resource_names_to_path()
{
	local resource
	local path=""

	for resource in "${@}"; do
		if [ "$path" == "" ]; then
			path="$resource"
		else
			path="${path}/$resource"
		fi
	done

	echo "$path"
}

devlink_resource_get()
{
	local name=$1
	local resource_name=.[][\"$DEVLINK_DEV\"]

	resource_name="$resource_name | .[] | select (.name == \"$name\")"

	shift
	for resource in "${@}"; do
		resource_name="${resource_name} | .[\"resources\"][] | \
			       select (.name == \"$resource\")"
	done

	devlink -j resource show "$DEVLINK_DEV" | jq "$resource_name"
}

devlink_resource_size_get()
{
	local size=$(devlink_resource_get "$@" | jq '.["size_new"]')

	if [ "$size" == "null" ]; then
		devlink_resource_get "$@" | jq '.["size"]'
	else
		echo "$size"
	fi
}

devlink_resource_size_set()
{
	local new_size=$1
	local path

	shift
	path=$(devlink_resource_names_to_path "$@")
	devlink resource set "$DEVLINK_DEV" path "$path" size "$new_size"
	check_err $? "Failed setting path $path to size $size"
}

devlink_resource_occ_get()
{
	devlink_resource_get "$@" | jq '.["occ"]'
}

devlink_reload()
{
	local still_pending

	devlink dev reload "$DEVLINK_DEV" &> /dev/null
	check_err $? "Failed reload"

	still_pending=$(devlink resource show "$DEVLINK_DEV" | \
			grep -c "size_new")
	check_err $still_pending "Failed reload - There are still unset sizes"
}

declare -A DEVLINK_ORIG

# Changing pool type from static to dynamic causes reinterpretation of threshold
# values. They therefore need to be saved before pool type is changed, then the
# pool type can be changed, and then the new values need to be set up. Therefore
# instead of saving the current state implicitly in the _set call, provide
# functions for all three primitives: save, set, and restore.

devlink_port_pool_threshold()
{
	local port=$1; shift
	local pool=$1; shift

	devlink sb port pool show $port pool $pool -j \
		| jq '.port_pool."'"$port"'"[].threshold'
}

devlink_port_pool_th_save()
{
	local port=$1; shift
	local pool=$1; shift
	local key="port_pool($port,$pool).threshold"

	DEVLINK_ORIG[$key]=$(devlink_port_pool_threshold $port $pool)
}

devlink_port_pool_th_set()
{
	local port=$1; shift
	local pool=$1; shift
	local th=$1; shift

	devlink sb port pool set $port pool $pool th $th
}

devlink_port_pool_th_restore()
{
	local port=$1; shift
	local pool=$1; shift
	local key="port_pool($port,$pool).threshold"
	local -a orig=(${DEVLINK_ORIG[$key]})

	if [[ -z $orig ]]; then
		echo "WARNING: Mismatched devlink_port_pool_th_restore"
	else
		devlink sb port pool set $port pool $pool th $orig
	fi
}

devlink_pool_size_thtype()
{
	local pool=$1; shift

	devlink sb pool show "$DEVLINK_DEV" pool $pool -j \
	    | jq -r '.pool[][] | (.size, .thtype)'
}

devlink_pool_size_thtype_save()
{
	local pool=$1; shift
	local key="pool($pool).size_thtype"

	DEVLINK_ORIG[$key]=$(devlink_pool_size_thtype $pool)
}

devlink_pool_size_thtype_set()
{
	local pool=$1; shift
	local thtype=$1; shift
	local size=$1; shift

	devlink sb pool set "$DEVLINK_DEV" pool $pool size $size thtype $thtype
}

devlink_pool_size_thtype_restore()
{
	local pool=$1; shift
	local key="pool($pool).size_thtype"
	local -a orig=(${DEVLINK_ORIG[$key]})

	if [[ -z ${orig[0]} ]]; then
		echo "WARNING: Mismatched devlink_pool_size_thtype_restore"
	else
		devlink sb pool set "$DEVLINK_DEV" pool $pool \
			size ${orig[0]} thtype ${orig[1]}
	fi
}

devlink_tc_bind_pool_th()
{
	local port=$1; shift
	local tc=$1; shift
	local dir=$1; shift

	devlink sb tc bind show $port tc $tc type $dir -j \
	    | jq -r '.tc_bind[][] | (.pool, .threshold)'
}

devlink_tc_bind_pool_th_save()
{
	local port=$1; shift
	local tc=$1; shift
	local dir=$1; shift
	local key="tc_bind($port,$dir,$tc).pool_th"

	DEVLINK_ORIG[$key]=$(devlink_tc_bind_pool_th $port $tc $dir)
}

devlink_tc_bind_pool_th_set()
{
	local port=$1; shift
	local tc=$1; shift
	local dir=$1; shift
	local pool=$1; shift
	local th=$1; shift

	devlink sb tc bind set $port tc $tc type $dir pool $pool th $th
}

devlink_tc_bind_pool_th_restore()
{
	local port=$1; shift
	local tc=$1; shift
	local dir=$1; shift
	local key="tc_bind($port,$dir,$tc).pool_th"
	local -a orig=(${DEVLINK_ORIG[$key]})

	if [[ -z ${orig[0]} ]]; then
		echo "WARNING: Mismatched devlink_tc_bind_pool_th_restore"
	else
		devlink sb tc bind set $port tc $tc type $dir \
			pool ${orig[0]} th ${orig[1]}
	fi
}

devlink_traps_num_get()
{
	devlink -j trap | jq '.[]["'$DEVLINK_DEV'"] | length'
}

devlink_traps_get()
{
	devlink -j trap | jq -r '.[]["'$DEVLINK_DEV'"][].name'
}

devlink_trap_type_get()
{
	local trap_name=$1; shift

	devlink -j trap show $DEVLINK_DEV trap $trap_name \
		| jq -r '.[][][].type'
}

devlink_trap_action_set()
{
	local trap_name=$1; shift
	local action=$1; shift

	# Pipe output to /dev/null to avoid expected warnings.
	devlink trap set $DEVLINK_DEV trap $trap_name \
		action $action &> /dev/null
}

devlink_trap_action_get()
{
	local trap_name=$1; shift

	devlink -j trap show $DEVLINK_DEV trap $trap_name \
		| jq -r '.[][][].action'
}

devlink_trap_group_get()
{
	devlink -j trap show $DEVLINK_DEV trap $trap_name \
		| jq -r '.[][][].group'
}

devlink_trap_metadata_test()
{
	local trap_name=$1; shift
	local metadata=$1; shift

	devlink -jv trap show $DEVLINK_DEV trap $trap_name \
		| jq -e '.[][][].metadata | contains(["'$metadata'"])' \
		&> /dev/null
}

devlink_trap_rx_packets_get()
{
	local trap_name=$1; shift

	devlink -js trap show $DEVLINK_DEV trap $trap_name \
		| jq '.[][][]["stats"]["rx"]["packets"]'
}

devlink_trap_rx_bytes_get()
{
	local trap_name=$1; shift

	devlink -js trap show $DEVLINK_DEV trap $trap_name \
		| jq '.[][][]["stats"]["rx"]["bytes"]'
}

devlink_trap_drop_packets_get()
{
	local trap_name=$1; shift

	devlink -js trap show $DEVLINK_DEV trap $trap_name \
		| jq '.[][][]["stats"]["rx"]["dropped"]'
}

devlink_trap_stats_idle_test()
{
	local trap_name=$1; shift
	local t0_packets t0_bytes
	local t1_packets t1_bytes

	t0_packets=$(devlink_trap_rx_packets_get $trap_name)
	t0_bytes=$(devlink_trap_rx_bytes_get $trap_name)

	sleep 1

	t1_packets=$(devlink_trap_rx_packets_get $trap_name)
	t1_bytes=$(devlink_trap_rx_bytes_get $trap_name)

	if [[ $t0_packets -eq $t1_packets && $t0_bytes -eq $t1_bytes ]]; then
		return 0
	else
		return 1
	fi
}

devlink_trap_drop_stats_idle_test()
{
	local trap_name=$1; shift
	local t0_packets t0_bytes

	t0_packets=$(devlink_trap_drop_packets_get $trap_name)

	sleep 1

	t1_packets=$(devlink_trap_drop_packets_get $trap_name)

	if [[ $t0_packets -eq $t1_packets ]]; then
		return 0
	else
		return 1
	fi
}

devlink_traps_enable_all()
{
	local trap_name

	for trap_name in $(devlink_traps_get); do
		devlink_trap_action_set $trap_name "trap"
	done
}

devlink_traps_disable_all()
{
	for trap_name in $(devlink_traps_get); do
		devlink_trap_action_set $trap_name "drop"
	done
}

devlink_trap_groups_get()
{
	devlink -j trap group | jq -r '.[]["'$DEVLINK_DEV'"][].name'
}

devlink_trap_group_action_set()
{
	local group_name=$1; shift
	local action=$1; shift

	# Pipe output to /dev/null to avoid expected warnings.
	devlink trap group set $DEVLINK_DEV group $group_name action $action \
		&> /dev/null
}

devlink_trap_group_rx_packets_get()
{
	local group_name=$1; shift

	devlink -js trap group show $DEVLINK_DEV group $group_name \
		| jq '.[][][]["stats"]["rx"]["packets"]'
}

devlink_trap_group_rx_bytes_get()
{
	local group_name=$1; shift

	devlink -js trap group show $DEVLINK_DEV group $group_name \
		| jq '.[][][]["stats"]["rx"]["bytes"]'
}

devlink_trap_group_stats_idle_test()
{
	local group_name=$1; shift
	local t0_packets t0_bytes
	local t1_packets t1_bytes

	t0_packets=$(devlink_trap_group_rx_packets_get $group_name)
	t0_bytes=$(devlink_trap_group_rx_bytes_get $group_name)

	sleep 1

	t1_packets=$(devlink_trap_group_rx_packets_get $group_name)
	t1_bytes=$(devlink_trap_group_rx_bytes_get $group_name)

	if [[ $t0_packets -eq $t1_packets && $t0_bytes -eq $t1_bytes ]]; then
		return 0
	else
		return 1
	fi
}

devlink_trap_exception_test()
{
	local trap_name=$1; shift
	local group_name

	group_name=$(devlink_trap_group_get $trap_name)

	devlink_trap_stats_idle_test $trap_name
	check_fail $? "Trap stats idle when packets should have been trapped"

	devlink_trap_group_stats_idle_test $group_name
	check_fail $? "Trap group idle when packets should have been trapped"
}

devlink_trap_drop_test()
{
	local trap_name=$1; shift
	local dev=$1; shift
	local handle=$1; shift
	local group_name

	group_name=$(devlink_trap_group_get $trap_name)

	# This is the common part of all the tests. It checks that stats are
	# initially idle, then non-idle after changing the trap action and
	# finally idle again. It also makes sure the packets are dropped and
	# never forwarded.
	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle with initial drop action"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle with initial drop action"

	devlink_trap_action_set $trap_name "trap"
	devlink_trap_stats_idle_test $trap_name
	check_fail $? "Trap stats idle after setting action to trap"
	devlink_trap_group_stats_idle_test $group_name
	check_fail $? "Trap group stats idle after setting action to trap"

	devlink_trap_action_set $trap_name "drop"

	devlink_trap_stats_idle_test $trap_name
	check_err $? "Trap stats not idle after setting action to drop"
	devlink_trap_group_stats_idle_test $group_name
	check_err $? "Trap group stats not idle after setting action to drop"

	tc_check_packets "dev $dev egress" $handle 0
	check_err $? "Packets were not dropped"
}

devlink_trap_drop_cleanup()
{
	local mz_pid=$1; shift
	local dev=$1; shift
	local proto=$1; shift
	local pref=$1; shift
	local handle=$1; shift

	kill $mz_pid && wait $mz_pid &> /dev/null
	tc filter del dev $dev egress protocol $proto pref $pref handle $handle flower
}

devlink_trap_stats_check()
{
	local trap_name=$1; shift
	local send_one="$@"
	local t0_packets
	local t1_packets

	t0_packets=$(devlink_trap_rx_packets_get $trap_name)

	$send_one && sleep 1

	t1_packets=$(devlink_trap_rx_packets_get $trap_name)

	[[ $t1_packets -ne $t0_packets ]]
}

devlink_trap_stats_test()
{
	local test_name=$1; shift

	RET=0

	devlink_trap_stats_check "$@"
	check_err $? "Trap stats did not increase"

	log_test "$test_name"
}

devlink_trap_policers_num_get()
{
	devlink -j -p trap policer show | jq '.[]["'$DEVLINK_DEV'"] | length'
}

devlink_trap_policer_rate_get()
{
	local policer_id=$1; shift

	devlink -j -p trap policer show $DEVLINK_DEV policer $policer_id \
		| jq '.[][][]["rate"]'
}

devlink_trap_policer_burst_get()
{
	local policer_id=$1; shift

	devlink -j -p trap policer show $DEVLINK_DEV policer $policer_id \
		| jq '.[][][]["burst"]'
}

devlink_trap_policer_rx_dropped_get()
{
	local policer_id=$1; shift

	devlink -j -p -s trap policer show $DEVLINK_DEV policer $policer_id \
		| jq '.[][][]["stats"]["rx"]["dropped"]'
}

devlink_trap_group_policer_get()
{
	local group_name=$1; shift

	devlink -j -p trap group show $DEVLINK_DEV group $group_name \
		| jq '.[][][]["policer"]'
}

devlink_port_by_netdev()
{
	local if_name=$1

	devlink -j port show $if_name | jq -e '.[] | keys' | jq -r '.[]'
}

devlink_cpu_port_get()
{
	local cpu_dl_port_num=$(devlink port list | grep "$DEVLINK_DEV" |
				grep cpu | cut -d/ -f3 | cut -d: -f1 |
				sed -n '1p')

	echo "$DEVLINK_DEV/$cpu_dl_port_num"
}

devlink_cell_size_get()
{
	devlink sb pool show "$DEVLINK_DEV" pool 0 -j \
	    | jq '.pool[][].cell_size'
}

devlink_pool_size_get()
{
	devlink sb show "$DEVLINK_DEV" -j | jq '.[][][]["size"]'
}

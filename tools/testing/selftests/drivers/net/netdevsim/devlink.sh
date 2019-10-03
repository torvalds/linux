#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="fw_flash_test params_test regions_test reload_test \
	   netns_reload_test resource_test"
NUM_NETIFS=0
source $lib_dir/lib.sh

BUS_ADDR=10
PORT_COUNT=4
DEV_NAME=netdevsim$BUS_ADDR
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV_NAME/net/
DEBUGFS_DIR=/sys/kernel/debug/netdevsim/$DEV_NAME/
DL_HANDLE=netdevsim/$DEV_NAME

fw_flash_test()
{
	RET=0

	devlink dev flash $DL_HANDLE file dummy
	check_err $? "Failed to flash with status updates on"

	echo "n"> $DEBUGFS_DIR/fw_update_status
	check_err $? "Failed to disable status updates"

	devlink dev flash $DL_HANDLE file dummy
	check_err $? "Failed to flash with status updates off"

	log_test "fw flash test"
}

param_get()
{
	local name=$1

	cmd_jq "devlink dev param show $DL_HANDLE name $name -j" \
	       '.[][][].values[] | select(.cmode == "driverinit").value'
}

param_set()
{
	local name=$1
	local value=$2

	devlink dev param set $DL_HANDLE name $name cmode driverinit value $value
}

check_value()
{
	local name=$1
	local phase_name=$2
	local expected_param_value=$3
	local expected_debugfs_value=$4
	local value

	value=$(param_get $name)
	check_err $? "Failed to get $name param value"
	[ "$value" == "$expected_param_value" ]
	check_err $? "Unexpected $phase_name $name param value"
	value=$(<$DEBUGFS_DIR/$name)
	check_err $? "Failed to get $name debugfs value"
	[ "$value" == "$expected_debugfs_value" ]
	check_err $? "Unexpected $phase_name $name debugfs value"
}

params_test()
{
	RET=0

	local max_macs
	local test1

	check_value max_macs initial 32 32
	check_value test1 initial true Y

	param_set max_macs 16
	check_err $? "Failed to set max_macs param value"
	param_set test1 false
	check_err $? "Failed to set test1 param value"

	check_value max_macs post-set 16 32
	check_value test1 post-set false Y

	devlink dev reload $DL_HANDLE

	check_value max_macs post-reload 16 16
	check_value test1 post-reload false N

	log_test "params test"
}

check_region_size()
{
	local name=$1
	local size

	size=$(devlink region show $DL_HANDLE/$name -j | jq -e -r '.[][].size')
	check_err $? "Failed to get $name region size"
	[ $size -eq 32768 ]
	check_err $? "Invalid $name region size"
}

check_region_snapshot_count()
{
	local name=$1
	local phase_name=$2
	local expected_count=$3
	local count

	count=$(devlink region show $DL_HANDLE/$name -j | jq -e -r '.[][].snapshot | length')
	[ $count -eq $expected_count ]
	check_err $? "Unexpected $phase_name snapshot count"
}

regions_test()
{
	RET=0

	local count

	check_region_size dummy
	check_region_snapshot_count dummy initial 0

	echo ""> $DEBUGFS_DIR/take_snapshot
	check_err $? "Failed to take first dummy region snapshot"
	check_region_snapshot_count dummy post-first-snapshot 1

	echo ""> $DEBUGFS_DIR/take_snapshot
	check_err $? "Failed to take second dummy region snapshot"
	check_region_snapshot_count dummy post-second-snapshot 2

	echo ""> $DEBUGFS_DIR/take_snapshot
	check_err $? "Failed to take third dummy region snapshot"
	check_region_snapshot_count dummy post-third-snapshot 3

	devlink region del $DL_HANDLE/dummy snapshot 1
	check_err $? "Failed to delete first dummy region snapshot"

	check_region_snapshot_count dummy post-first-delete 2

	log_test "regions test"
}

reload_test()
{
	RET=0

	devlink dev reload $DL_HANDLE
	check_err $? "Failed to reload"

	log_test "reload test"
}

netns_reload_test()
{
	RET=0

	ip netns add testns1
	check_err $? "Failed add netns \"testns1\""
	ip netns add testns2
	check_err $? "Failed add netns \"testns2\""

	devlink dev reload $DL_HANDLE netns testns1
	check_err $? "Failed to reload into netns \"testns1\""

	devlink -N testns1 dev reload $DL_HANDLE netns testns2
	check_err $? "Failed to reload from netns \"testns1\" into netns \"testns2\""

	ip netns del testns2
	ip netns del testns1

	log_test "netns reload test"
}

DUMMYDEV="dummytest"

res_val_get()
{
	local netns=$1
	local parentname=$2
	local name=$3
	local type=$4

	cmd_jq "devlink -N $netns resource show $DL_HANDLE -j" \
	       ".[][][] | select(.name == \"$parentname\").resources[] \
	        | select(.name == \"$name\").$type"
}

resource_test()
{
	RET=0

	ip netns add testns1
	check_err $? "Failed add netns \"testns1\""
	ip netns add testns2
	check_err $? "Failed add netns \"testns2\""

	devlink dev reload $DL_HANDLE netns testns1
	check_err $? "Failed to reload into netns \"testns1\""

	# Create dummy dev to add the address and routes on.

	ip -n testns1 link add name $DUMMYDEV type dummy
	check_err $? "Failed create dummy device"
	ip -n testns1 link set $DUMMYDEV up
	check_err $? "Failed bring up dummy device"
	ip -n testns1 a a 192.0.1.1/24 dev $DUMMYDEV
	check_err $? "Failed add an IP address to dummy device"

	local occ=$(res_val_get testns1 IPv4 fib occ)
	local limit=$((occ+1))

	# Set fib size limit to handle one another route only.

	devlink -N testns1 resource set $DL_HANDLE path IPv4/fib size $limit
	check_err $? "Failed to set IPv4/fib resource size"
	local size_new=$(res_val_get testns1 IPv4 fib size_new)
	[ "$size_new" -eq "$limit" ]
	check_err $? "Unexpected \"size_new\" value (got $size_new, expected $limit)"

	devlink -N testns1 dev reload $DL_HANDLE
	check_err $? "Failed to reload"
	local size=$(res_val_get testns1 IPv4 fib size)
	[ "$size" -eq "$limit" ]
	check_err $? "Unexpected \"size\" value (got $size, expected $limit)"

	# Insert 2 routes, the first is going to be inserted,
	# the second is expected to fail to be inserted.

	ip -n testns1 r a 192.0.2.0/24 via 192.0.1.2
	check_err $? "Failed to add route"

	ip -n testns1 r a 192.0.3.0/24 via 192.0.1.2
	check_fail $? "Unexpected successful route add over limit"

	# Now create another dummy in second network namespace and
	# insert two routes. That is over the limit of the netdevsim
	# instance in the first namespace. Move the netdevsim instance
	# into the second namespace and expect it to fail.

	ip -n testns2 link add name $DUMMYDEV type dummy
	check_err $? "Failed create dummy device"
	ip -n testns2 link set $DUMMYDEV up
	check_err $? "Failed bring up dummy device"
	ip -n testns2 a a 192.0.1.1/24 dev $DUMMYDEV
	check_err $? "Failed add an IP address to dummy device"
	ip -n testns2 r a 192.0.2.0/24 via 192.0.1.2
	check_err $? "Failed to add route"
	ip -n testns2 r a 192.0.3.0/24 via 192.0.1.2
	check_err $? "Failed to add route"

	devlink -N testns1 dev reload $DL_HANDLE netns testns2
	check_fail $? "Unexpected successful reload from netns \"testns1\" into netns \"testns2\""

	ip netns del testns2
	ip netns del testns1

	log_test "resource test"
}

setup_prepare()
{
	modprobe netdevsim
	echo "$BUS_ADDR $PORT_COUNT" > /sys/bus/netdevsim/new_device
	while [ ! -d $SYSFS_NET_DIR ] ; do :; done
}

cleanup()
{
	pre_cleanup
	echo "$BUS_ADDR" > /sys/bus/netdevsim/del_device
	modprobe -r netdevsim
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS

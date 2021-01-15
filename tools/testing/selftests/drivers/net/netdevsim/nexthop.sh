#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# This test is for checking the nexthop offload API. It makes use of netdevsim
# which registers a listener to the nexthop notification chain.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	nexthop_single_add_test
	nexthop_single_add_err_test
	nexthop_group_add_test
	nexthop_group_add_err_test
	nexthop_group_replace_test
	nexthop_group_replace_err_test
	nexthop_single_replace_test
	nexthop_single_replace_err_test
	nexthop_single_in_group_replace_test
	nexthop_single_in_group_replace_err_test
	nexthop_single_in_group_delete_test
	nexthop_single_in_group_delete_err_test
	nexthop_replay_test
	nexthop_replay_err_test
"
NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
DEVLINK_DEV=netdevsim/${DEV}
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV/net/
NUM_NETIFS=0
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

nexthop_check()
{
	local nharg="$1"; shift
	local expected="$1"; shift

	out=$($IP nexthop show ${nharg} | sed -e 's/ *$//')
	if [[ "$out" != "$expected" ]]; then
		return 1
	fi

	return 0
}

nexthop_resource_check()
{
	local expected_occ=$1; shift

	occ=$($DEVLINK -jp resource show $DEVLINK_DEV \
		| jq '.[][][] | select(.name=="nexthops") | .["occ"]')

	if [ $expected_occ -ne $occ ]; then
		return 1
	fi

	return 0
}

nexthop_resource_set()
{
	local size=$1; shift

	$DEVLINK resource set $DEVLINK_DEV path nexthops size $size
	$DEVLINK dev reload $DEVLINK_DEV
}

nexthop_single_add_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	nexthop_check "id 1" "id 1 via 192.0.2.2 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry"

	nexthop_resource_check 1
	check_err $? "Wrong nexthop occupancy"

	$IP nexthop del id 1
	nexthop_resource_check 0
	check_err $? "Wrong nexthop occupancy after delete"

	log_test "Single nexthop add and delete"
}

nexthop_single_add_err_test()
{
	RET=0

	nexthop_resource_set 1

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1

	$IP nexthop add id 2 via 192.0.2.3 dev dummy1 &> /dev/null
	check_fail $? "Nexthop addition succeeded when should fail"

	nexthop_resource_check 1
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop add failure"

	$IP nexthop flush &> /dev/null
	nexthop_resource_set 9999
}

nexthop_group_add_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	$IP nexthop add id 10 group 1/2
	nexthop_check "id 10" "id 10 group 1/2 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_resource_check 4
	check_err $? "Wrong nexthop occupancy"

	$IP nexthop del id 10
	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy after delete"

	$IP nexthop add id 10 group 1,20/2,39
	nexthop_check "id 10" "id 10 group 1,20/2,39 trap"
	check_err $? "Unexpected weighted nexthop group entry"

	nexthop_resource_check 61
	check_err $? "Wrong weighted nexthop occupancy"

	$IP nexthop del id 10
	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy after delete"

	log_test "Nexthop group add and delete"

	$IP nexthop flush &> /dev/null
}

nexthop_group_add_err_test()
{
	RET=0

	nexthop_resource_set 2

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	$IP nexthop add id 10 group 1/2 &> /dev/null
	check_fail $? "Nexthop group addition succeeded when should fail"

	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy"

	log_test "Nexthop group add failure"

	$IP nexthop flush &> /dev/null
	nexthop_resource_set 9999
}

nexthop_group_replace_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.4 dev dummy1
	$IP nexthop add id 10 group 1/2

	$IP nexthop replace id 10 group 1/2/3
	nexthop_check "id 10" "id 10 group 1/2/3 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_resource_check 6
	check_err $? "Wrong nexthop occupancy"

	log_test "Nexthop group replace"

	$IP nexthop flush &> /dev/null
}

nexthop_group_replace_err_test()
{
	RET=0

	nexthop_resource_set 5

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.4 dev dummy1
	$IP nexthop add id 10 group 1/2

	$IP nexthop replace id 10 group 1/2/3 &> /dev/null
	check_fail $? "Nexthop group replacement succeeded when should fail"

	nexthop_check "id 10" "id 10 group 1/2 trap"
	check_err $? "Unexpected nexthop group entry after failure"

	nexthop_resource_check 5
	check_err $? "Wrong nexthop occupancy after failure"

	log_test "Nexthop group replace failure"

	$IP nexthop flush &> /dev/null
	nexthop_resource_set 9999
}

nexthop_single_replace_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1

	$IP nexthop replace id 1 via 192.0.2.3 dev dummy1
	nexthop_check "id 1" "id 1 via 192.0.2.3 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry"

	nexthop_resource_check 1
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop replace"

	$IP nexthop flush &> /dev/null
}

nexthop_single_replace_err_test()
{
	RET=0

	# This is supposed to cause the replace to fail because the new nexthop
	# is programmed before deleting the replaced one.
	nexthop_resource_set 1

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1

	$IP nexthop replace id 1 via 192.0.2.3 dev dummy1 &> /dev/null
	check_fail $? "Nexthop replace succeeded when should fail"

	nexthop_check "id 1" "id 1 via 192.0.2.2 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry after failure"

	nexthop_resource_check 1
	check_err $? "Wrong nexthop occupancy after failure"

	log_test "Single nexthop replace failure"

	$IP nexthop flush &> /dev/null
	nexthop_resource_set 9999
}

nexthop_single_in_group_replace_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2

	$IP nexthop replace id 1 via 192.0.2.4 dev dummy1
	check_err $? "Failed to replace nexthop when should not"

	nexthop_check "id 10" "id 10 group 1/2 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_resource_check 4
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop replace while in group"

	$IP nexthop flush &> /dev/null
}

nexthop_single_in_group_replace_err_test()
{
	RET=0

	nexthop_resource_set 5

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2

	$IP nexthop replace id 1 via 192.0.2.4 dev dummy1 &> /dev/null
	check_fail $? "Nexthop replacement succeeded when should fail"

	nexthop_check "id 1" "id 1 via 192.0.2.2 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry after failure"

	nexthop_check "id 10" "id 10 group 1/2 trap"
	check_err $? "Unexpected nexthop group entry after failure"

	nexthop_resource_check 4
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop replace while in group failure"

	$IP nexthop flush &> /dev/null
	nexthop_resource_set 9999
}

nexthop_single_in_group_delete_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2

	$IP nexthop del id 1
	nexthop_check "id 10" "id 10 group 2 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop delete while in group"

	$IP nexthop flush &> /dev/null
}

nexthop_single_in_group_delete_err_test()
{
	RET=0

	# First, nexthop 1 will be deleted, which will reduce the occupancy to
	# 5. Afterwards, a replace notification will be sent for nexthop group
	# 10 with only two nexthops. Since the new group is allocated before
	# the old is deleted, the replacement will fail as it will result in an
	# occupancy of 7.
	nexthop_resource_set 6

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.4 dev dummy1
	$IP nexthop add id 10 group 1/2/3

	$IP nexthop del id 1

	nexthop_resource_check 5
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop delete while in group failure"

	$IP nexthop flush &> /dev/null
	nexthop_resource_set 9999
}

nexthop_replay_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2

	$DEVLINK dev reload $DEVLINK_DEV
	check_err $? "Failed to reload when should not"

	nexthop_check "id 1" "id 1 via 192.0.2.2 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry after reload"

	nexthop_check "id 2" "id 2 via 192.0.2.3 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry after reload"

	nexthop_check "id 10" "id 10 group 1/2 trap"
	check_err $? "Unexpected nexthop group entry after reload"

	nexthop_resource_check 4
	check_err $? "Wrong nexthop occupancy"

	log_test "Nexthop replay"

	$IP nexthop flush &> /dev/null
}

nexthop_replay_err_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2

	# Reduce size of nexthop resource so that reload will fail.
	$DEVLINK resource set $DEVLINK_DEV path nexthops size 3
	$DEVLINK dev reload $DEVLINK_DEV &> /dev/null
	check_fail $? "Reload succeeded when should fail"

	$DEVLINK resource set $DEVLINK_DEV path nexthops size 9999
	$DEVLINK dev reload $DEVLINK_DEV
	check_err $? "Failed to reload when should not"

	log_test "Nexthop replay failure"

	$IP nexthop flush &> /dev/null
}

setup_prepare()
{
	local netdev

	modprobe netdevsim &> /dev/null

	echo "$DEV_ADDR 1" > ${NETDEVSIM_PATH}/new_device
	while [ ! -d $SYSFS_NET_DIR ] ; do :; done

	set -e

	ip netns add testns1
	devlink dev reload $DEVLINK_DEV netns testns1

	IP="ip -netns testns1"
	DEVLINK="devlink -N testns1"

	$IP link add name dummy1 up type dummy
	$IP address add 192.0.2.1/24 dev dummy1

	set +e
}

cleanup()
{
	pre_cleanup
	ip netns del testns1
	echo "$DEV_ADDR" > ${NETDEVSIM_PATH}/del_device
	modprobe -r netdevsim &> /dev/null
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS

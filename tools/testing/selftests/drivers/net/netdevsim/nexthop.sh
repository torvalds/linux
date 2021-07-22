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
	nexthop_res_group_add_test
	nexthop_res_group_add_err_test
	nexthop_group_replace_test
	nexthop_group_replace_err_test
	nexthop_res_group_replace_test
	nexthop_res_group_replace_err_test
	nexthop_res_group_idle_timer_test
	nexthop_res_group_idle_timer_del_test
	nexthop_res_group_increase_idle_timer_test
	nexthop_res_group_decrease_idle_timer_test
	nexthop_res_group_unbalanced_timer_test
	nexthop_res_group_unbalanced_timer_del_test
	nexthop_res_group_no_unbalanced_timer_test
	nexthop_res_group_short_unbalanced_timer_test
	nexthop_res_group_increase_unbalanced_timer_test
	nexthop_res_group_decrease_unbalanced_timer_test
	nexthop_res_group_force_migrate_busy_test
	nexthop_single_replace_test
	nexthop_single_replace_err_test
	nexthop_single_in_group_replace_test
	nexthop_single_in_group_replace_err_test
	nexthop_single_in_res_group_replace_test
	nexthop_single_in_res_group_replace_err_test
	nexthop_single_in_group_delete_test
	nexthop_single_in_group_delete_err_test
	nexthop_single_in_res_group_delete_test
	nexthop_single_in_res_group_delete_err_test
	nexthop_replay_test
	nexthop_replay_err_test
"
NETDEVSIM_PATH=/sys/bus/netdevsim/
DEV_ADDR=1337
DEV=netdevsim${DEV_ADDR}
SYSFS_NET_DIR=/sys/bus/netdevsim/devices/$DEV/net/
DEBUGFS_NET_DIR=/sys/kernel/debug/netdevsim/$DEV/
NUM_NETIFS=0
source $lib_dir/lib.sh

DEVLINK_DEV=
source $lib_dir/devlink_lib.sh
DEVLINK_DEV=netdevsim/${DEV}

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

nexthop_bucket_nhid_count_check()
{
	local group_id=$1; shift
	local expected
	local count
	local nhid
	local ret

	while (($# > 0)); do
		nhid=$1; shift
		expected=$1; shift

		count=$($IP nexthop bucket show id $group_id nhid $nhid |
			grep "trap" | wc -l)
		if ((expected != count)); then
			return 1
		fi
	done

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

nexthop_res_group_add_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	$IP nexthop add id 10 group 1/2 type resilient buckets 4
	nexthop_check "id 10" "id 10 group 1/2 type resilient buckets 4 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_bucket_nhid_count_check 10 1 2
	check_err $? "Wrong nexthop buckets count"
	nexthop_bucket_nhid_count_check 10 2 2
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 6
	check_err $? "Wrong nexthop occupancy"

	$IP nexthop del id 10
	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy after delete"

	$IP nexthop add id 10 group 1,3/2,2 type resilient buckets 5
	nexthop_check "id 10" "id 10 group 1,3/2,2 type resilient buckets 5 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected weighted nexthop group entry"

	nexthop_bucket_nhid_count_check 10 1 3
	check_err $? "Wrong nexthop buckets count"
	nexthop_bucket_nhid_count_check 10 2 2
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 7
	check_err $? "Wrong weighted nexthop occupancy"

	$IP nexthop del id 10
	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy after delete"

	log_test "Resilient nexthop group add and delete"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_add_err_test()
{
	RET=0

	nexthop_resource_set 2

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	$IP nexthop add id 10 group 1/2 type resilient buckets 4 &> /dev/null
	check_fail $? "Nexthop group addition succeeded when should fail"

	nexthop_resource_check 2
	check_err $? "Wrong nexthop occupancy"

	log_test "Resilient nexthop group add failure"

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

nexthop_res_group_replace_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.4 dev dummy1
	$IP nexthop add id 10 group 1/2 type resilient buckets 6

	$IP nexthop replace id 10 group 1/2/3 type resilient
	nexthop_check "id 10" "id 10 group 1/2/3 type resilient buckets 6 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_bucket_nhid_count_check 10 1 2
	check_err $? "Wrong nexthop buckets count"
	nexthop_bucket_nhid_count_check 10 2 2
	check_err $? "Wrong nexthop buckets count"
	nexthop_bucket_nhid_count_check 10 3 2
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 9
	check_err $? "Wrong nexthop occupancy"

	log_test "Resilient nexthop group replace"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_replace_err_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.4 dev dummy1
	$IP nexthop add id 10 group 1/2 type resilient buckets 6

	ip netns exec testns1 \
		echo 1 > $DEBUGFS_NET_DIR/fib/fail_res_nexthop_group_replace
	$IP nexthop replace id 10 group 1/2/3 type resilient &> /dev/null
	check_fail $? "Nexthop group replacement succeeded when should fail"

	nexthop_check "id 10" "id 10 group 1/2 type resilient buckets 6 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected nexthop group entry after failure"

	nexthop_bucket_nhid_count_check 10 1 3
	check_err $? "Wrong nexthop buckets count"
	nexthop_bucket_nhid_count_check 10 2 3
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 9
	check_err $? "Wrong nexthop occupancy after failure"

	log_test "Resilient nexthop group replace failure"

	$IP nexthop flush &> /dev/null
	ip netns exec testns1 \
		echo 0 > $DEBUGFS_NET_DIR/fib/fail_res_nexthop_group_replace
}

nexthop_res_mark_buckets_busy()
{
	local group_id=$1; shift
	local nhid=$1; shift
	local count=$1; shift
	local index

	for index in $($IP -j nexthop bucket show id $group_id nhid $nhid |
		       jq '.[].bucket.index' | head -n ${count:--0})
	do
		echo $group_id $index \
			> $DEBUGFS_NET_DIR/fib/nexthop_bucket_activity
	done
}

nexthop_res_num_nhid_buckets()
{
	local group_id=$1; shift
	local nhid=$1; shift

	$IP -j nexthop bucket show id $group_id nhid $nhid | jq length
}

nexthop_res_group_idle_timer_test()
{
	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient buckets 8 idle_timer 4
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	nexthop_bucket_nhid_count_check 10  1 4  2 4
	check_err $? "Group expected to be unbalanced"

	sleep 6

	nexthop_bucket_nhid_count_check 10  1 2  2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after idle timer"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_idle_timer_del_test()
{
	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1,50/2,50/3,1 \
	    type resilient buckets 8 idle_timer 6
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1,50/2,150/3,1 type resilient

	nexthop_bucket_nhid_count_check 10  1 4  2 4  3 0
	check_err $? "Group expected to be unbalanced"

	sleep 4

	# Deletion prompts group replacement. Check that the bucket timers
	# are kept.
	$IP nexthop delete id 3

	nexthop_bucket_nhid_count_check 10  1 4  2 4
	check_err $? "Group expected to still be unbalanced"

	sleep 4

	nexthop_bucket_nhid_count_check 10  1 2  2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after idle timer (with delete)"

	$IP nexthop flush &> /dev/null
}

__nexthop_res_group_increase_timer_test()
{
	local timer=$1; shift

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient buckets 8 $timer 4
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group expected to be unbalanced"

	sleep 2
	$IP nexthop replace id 10 group 1/2,3 type resilient $timer 8
	sleep 4

	# 6 seconds, past the original timer.
	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group still expected to be unbalanced"

	sleep 4

	# 10 seconds, past the new timer.
	nexthop_bucket_nhid_count_check 10 2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after $timer increase"

	$IP nexthop flush &> /dev/null
}

__nexthop_res_group_decrease_timer_test()
{
	local timer=$1; shift

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient buckets 8 $timer 8
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group expected to be unbalanced"

	sleep 2
	$IP nexthop replace id 10 group 1/2,3 type resilient $timer 4
	sleep 4

	# 6 seconds, past the new timer, before the old timer.
	nexthop_bucket_nhid_count_check 10 2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after $timer decrease"

	$IP nexthop flush &> /dev/null
}

__nexthop_res_group_increase_timer_del_test()
{
	local timer=$1; shift

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1,100/2,100/3,1 \
	    type resilient buckets 8 $timer 4
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1,100/2,300/3,1 type resilient

	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group expected to be unbalanced"

	sleep 2
	$IP nexthop replace id 10 group 1/2,3 type resilient $timer 8
	sleep 4

	# 6 seconds, past the original timer.
	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group still expected to be unbalanced"

	sleep 4

	# 10 seconds, past the new timer.
	nexthop_bucket_nhid_count_check 10 2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after $timer increase"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_increase_idle_timer_test()
{
	__nexthop_res_group_increase_timer_test idle_timer
}

nexthop_res_group_decrease_idle_timer_test()
{
	__nexthop_res_group_decrease_timer_test idle_timer
}

nexthop_res_group_unbalanced_timer_test()
{
	local i

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient \
	    buckets 8 idle_timer 6 unbalanced_timer 10
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	for i in 1 2; do
		sleep 4
		nexthop_bucket_nhid_count_check 10  1 4  2 4
		check_err $? "$i: Group expected to be unbalanced"
		nexthop_res_mark_buckets_busy 10 1
	done

	# 3 x sleep 4 > unbalanced timer 10
	sleep 4
	nexthop_bucket_nhid_count_check 10  1 2  2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after unbalanced timer"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_unbalanced_timer_del_test()
{
	local i

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1,50/2,50/3,1 type resilient \
	    buckets 8 idle_timer 6 unbalanced_timer 10
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1,50/2,150/3,1 type resilient

	# Check that NH delete does not reset unbalanced time.
	sleep 4
	$IP nexthop delete id 3
	nexthop_bucket_nhid_count_check 10  1 4  2 4
	check_err $? "1: Group expected to be unbalanced"
	nexthop_res_mark_buckets_busy 10 1

	sleep 4
	nexthop_bucket_nhid_count_check 10  1 4  2 4
	check_err $? "2: Group expected to be unbalanced"
	nexthop_res_mark_buckets_busy 10 1

	# 3 x sleep 4 > unbalanced timer 10
	sleep 4
	nexthop_bucket_nhid_count_check 10  1 2  2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after unbalanced timer (with delete)"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_no_unbalanced_timer_test()
{
	local i

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient buckets 8
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	for i in $(seq 3); do
		sleep 60
		nexthop_bucket_nhid_count_check 10 2 6
		check_fail $? "$i: Group expected to be unbalanced"
		nexthop_res_mark_buckets_busy 10 1
	done

	log_test "Buckets never force-migrated without unbalanced timer"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_short_unbalanced_timer_test()
{
	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient \
	    buckets 8 idle_timer 120 unbalanced_timer 4
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group expected to be unbalanced"

	sleep 5

	nexthop_bucket_nhid_count_check 10 2 6
	check_err $? "Group expected to be balanced"

	log_test "Bucket migration after unbalanced < idle timer"

	$IP nexthop flush &> /dev/null
}

nexthop_res_group_increase_unbalanced_timer_test()
{
	__nexthop_res_group_increase_timer_test unbalanced_timer
}

nexthop_res_group_decrease_unbalanced_timer_test()
{
	__nexthop_res_group_decrease_timer_test unbalanced_timer
}

nexthop_res_group_force_migrate_busy_test()
{
	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1

	RET=0

	$IP nexthop add id 10 group 1/2 type resilient \
	    buckets 8 idle_timer 120
	nexthop_res_mark_buckets_busy 10 1
	$IP nexthop replace id 10 group 1/2,3 type resilient

	nexthop_bucket_nhid_count_check 10 2 6
	check_fail $? "Group expected to be unbalanced"

	$IP nexthop replace id 10 group 2 type resilient
	nexthop_bucket_nhid_count_check 10 2 8
	check_err $? "All buckets expected to have migrated"

	log_test "Busy buckets force-migrated when NH removed"

	$IP nexthop flush &> /dev/null
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

nexthop_single_in_res_group_replace_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2 type resilient buckets 4

	$IP nexthop replace id 1 via 192.0.2.4 dev dummy1
	check_err $? "Failed to replace nexthop when should not"

	nexthop_check "id 10" "id 10 group 1/2 type resilient buckets 4 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_bucket_nhid_count_check 10  1 2  2 2
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 6
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop replace while in resilient group"

	$IP nexthop flush &> /dev/null
}

nexthop_single_in_res_group_replace_err_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2 type resilient buckets 4

	ip netns exec testns1 \
		echo 1 > $DEBUGFS_NET_DIR/fib/fail_nexthop_bucket_replace
	$IP nexthop replace id 1 via 192.0.2.4 dev dummy1 &> /dev/null
	check_fail $? "Nexthop replacement succeeded when should fail"

	nexthop_check "id 1" "id 1 via 192.0.2.2 dev dummy1 scope link trap"
	check_err $? "Unexpected nexthop entry after failure"

	nexthop_check "id 10" "id 10 group 1/2 type resilient buckets 4 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected nexthop group entry after failure"

	nexthop_bucket_nhid_count_check 10  1 2  2 2
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 6
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop replace while in resilient group failure"

	$IP nexthop flush &> /dev/null
	ip netns exec testns1 \
		echo 0 > $DEBUGFS_NET_DIR/fib/fail_nexthop_bucket_replace
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

nexthop_single_in_res_group_delete_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 10 group 1/2 type resilient buckets 4

	$IP nexthop del id 1
	nexthop_check "id 10" "id 10 group 2 type resilient buckets 4 idle_timer 120 unbalanced_timer 0 unbalanced_time 0 trap"
	check_err $? "Unexpected nexthop group entry"

	nexthop_bucket_nhid_count_check 10 2 4
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 5
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop delete while in resilient group"

	$IP nexthop flush &> /dev/null
}

nexthop_single_in_res_group_delete_err_test()
{
	RET=0

	$IP nexthop add id 1 via 192.0.2.2 dev dummy1
	$IP nexthop add id 2 via 192.0.2.3 dev dummy1
	$IP nexthop add id 3 via 192.0.2.4 dev dummy1
	$IP nexthop add id 10 group 1/2/3 type resilient buckets 6

	ip netns exec testns1 \
		echo 1 > $DEBUGFS_NET_DIR/fib/fail_nexthop_bucket_replace
	$IP nexthop del id 1

	# We failed to replace the two nexthop buckets that were originally
	# assigned to nhid 1.
	nexthop_bucket_nhid_count_check 10  2 2  3 2
	check_err $? "Wrong nexthop buckets count"

	nexthop_resource_check 8
	check_err $? "Wrong nexthop occupancy"

	log_test "Single nexthop delete while in resilient group failure"

	$IP nexthop flush &> /dev/null
	ip netns exec testns1 \
		echo 0 > $DEBUGFS_NET_DIR/fib/fail_nexthop_bucket_replace
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

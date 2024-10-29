#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking the A-TCAM and C-TCAM operation in Spectrum-2.
# It tries to exercise as many code paths in the eRP state machine as
# possible.

lib_dir=$(dirname $0)/../../../../net/forwarding

ALL_TESTS="single_mask_test identical_filters_test two_masks_test \
	multiple_masks_test ctcam_edge_cases_test delta_simple_test \
	delta_two_masks_one_key_test delta_simple_rehash_test \
	bloom_simple_test bloom_complex_test bloom_delta_test \
	max_erp_entries_test max_group_size_test collision_test"
NUM_NETIFS=2
source $lib_dir/lib.sh
source $lib_dir/tc_common.sh
source $lib_dir/devlink_lib.sh

tcflags="skip_hw"

h1_create()
{
	simple_if_init $h1 192.0.2.1/24 198.51.100.1/24
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24 198.51.100.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24 198.51.100.2/24
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/24 198.51.100.2/24
}

tp_record()
{
	local tracepoint=$1
	local cmd=$2

	perf record -q -e $tracepoint $cmd
	return $?
}

tp_record_all()
{
	local tracepoint=$1
	local seconds=$2

	perf record -a -q -e $tracepoint sleep $seconds
	return $?
}

__tp_hit_count()
{
	local tracepoint=$1

	local perf_output=`perf script -F trace:event,trace`
	return `echo $perf_output | grep "$tracepoint:" | wc -l`
}

tp_check_hits()
{
	local tracepoint=$1
	local count=$2

	__tp_hit_count $tracepoint
	if [[ "$?" -ne "$count" ]]; then
		return 1
	fi
	return 0
}

tp_check_hits_any()
{
	local tracepoint=$1

	__tp_hit_count $tracepoint
	if [[ "$?" -eq "0" ]]; then
		return 1
	fi
	return 0
}

single_mask_test()
{
	# When only a single mask is required, the device uses the master
	# mask and not the eRP table. Verify that under this mode the right
	# filter is matched

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Single filter - did not match"

	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 198.51.100.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 2
	check_err $? "Two filters - did not match highest priority"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 198.51.100.1 -B 198.51.100.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Two filters - did not match lowest priority"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 198.51.100.1 -B 198.51.100.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_err $? "Single filter - did not match after delete"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "single mask test ($tcflags)"
}

identical_filters_test()
{
	# When two filters that only differ in their priority are used,
	# one needs to be inserted into the C-TCAM. This test verifies
	# that filters are correctly spilled to C-TCAM and that the right
	# filter is matched

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match A-TCAM filter"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match C-TCAM filter after A-TCAM delete"

	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_err $? "Did not match C-TCAM filter after A-TCAM add"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Did not match A-TCAM filter after C-TCAM delete"

	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower

	log_test "identical filters test ($tcflags)"
}

two_masks_test()
{
	# When more than one mask is required, the eRP table is used. This
	# test verifies that the eRP table is correctly allocated and used

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.0.0/8 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Two filters - did not match highest priority"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Single filter - did not match"

	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.0/24 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Two filters - did not match highest priority after add"

	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "two masks test ($tcflags)"
}

multiple_masks_test()
{
	# The number of masks in a region is limited. Once the maximum
	# number of masks has been reached filters that require new
	# masks are spilled to the C-TCAM. This test verifies that
	# spillage is performed correctly and that the right filter is
	# matched

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	local index

	RET=0

	NUM_MASKS=32
	NUM_ERPS=16
	BASE_INDEX=100

	for i in $(eval echo {1..$NUM_MASKS}); do
		index=$((BASE_INDEX - i))

		if ((i > NUM_ERPS)); then
			exp_hits=1
			err_msg="$i filters - C-TCAM spill did not happen when it was expected"
		else
			exp_hits=0
			err_msg="$i filters - C-TCAM spill happened when it should not"
		fi

		tp_record "mlxsw:mlxsw_sp_acl_atcam_entry_add_ctcam_spill" \
			"tc filter add dev $h2 ingress protocol ip pref $index \
				handle $index \
				flower $tcflags \
				dst_ip 192.0.2.2/${i} src_ip 192.0.2.1/${i} \
				action drop"
		tp_check_hits "mlxsw:mlxsw_sp_acl_atcam_entry_add_ctcam_spill" \
				$exp_hits
		check_err $? "$err_msg"

		$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 \
			-B 192.0.2.2 -t ip -q

		tc_check_packets "dev $h2 ingress" $index 1
		check_err $? "$i filters - did not match highest priority (add)"
	done

	for i in $(eval echo {$NUM_MASKS..1}); do
		index=$((BASE_INDEX - i))

		$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 \
			-B 192.0.2.2 -t ip -q

		tc_check_packets "dev $h2 ingress" $index 2
		check_err $? "$i filters - did not match highest priority (del)"

		tc filter del dev $h2 ingress protocol ip pref $index \
			handle $index flower
	done

	log_test "multiple masks test ($tcflags)"
}

ctcam_two_atcam_masks_test()
{
	RET=0

	# First case: C-TCAM is disabled when there are two A-TCAM masks.
	# We push a filter into the C-TCAM by using two identical filters
	# as in identical_filters_test()

	# Filter goes into A-TCAM
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	# Filter goes into C-TCAM
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	# Filter goes into A-TCAM
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.0.0/16 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match A-TCAM filter"

	# Delete both A-TCAM and C-TCAM filters and make sure the remaining
	# A-TCAM filter still works
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Did not match A-TCAM filter"

	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower

	log_test "ctcam with two atcam masks test ($tcflags)"
}

ctcam_one_atcam_mask_test()
{
	RET=0

	# Second case: C-TCAM is disabled when there is one A-TCAM mask.
	# The test is similar to identical_filters_test()

	# Filter goes into A-TCAM
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	# Filter goes into C-TCAM
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match C-TCAM filter"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match A-TCAM filter"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "ctcam with one atcam mask test ($tcflags)"
}

ctcam_no_atcam_masks_test()
{
	RET=0

	# Third case: C-TCAM is disabled when there are no A-TCAM masks
	# This test exercises the code path that transitions the eRP table
	# to its initial state after deleting the last C-TCAM mask

	# Filter goes into A-TCAM
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	# Filter goes into C-TCAM
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "ctcam with no atcam masks test ($tcflags)"
}

ctcam_edge_cases_test()
{
	# When the C-TCAM is disabled after deleting the last C-TCAM
	# mask, we want to make sure the eRP state machine is put in
	# the correct state

	ctcam_two_atcam_masks_test
	ctcam_one_atcam_mask_test
	ctcam_no_atcam_masks_test
}

delta_simple_test()
{
	# The first filter will create eRP, the second filter will fit into
	# the first eRP with delta. Remove the first rule then and check that
        # the eRP stays (referenced by the second filter).

	RET=0

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	tp_record "objagg:*" "tc filter add dev $h2 ingress protocol ip \
		   pref 1 handle 101 flower $tcflags dst_ip 192.0.0.0/24 \
		   action drop"
	tp_check_hits "objagg:objagg_obj_root_create" 1
	check_err $? "eRP was not created"

	tp_record "objagg:*" "tc filter add dev $h2 ingress protocol ip \
		   pref 2 handle 102 flower $tcflags dst_ip 192.0.2.2 \
		   action drop"
	tp_check_hits "objagg:objagg_obj_root_create" 0
	check_err $? "eRP was incorrectly created"
	tp_check_hits "objagg:objagg_obj_parent_assign" 1
	check_err $? "delta was not created"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tp_record "objagg:*" "tc filter del dev $h2 ingress protocol ip \
		   pref 1 handle 101 flower"
	tp_check_hits "objagg:objagg_obj_root_destroy" 0
	check_err $? "eRP was incorrectly destroyed"
	tp_check_hits "objagg:objagg_obj_parent_unassign" 0
	check_err $? "delta was incorrectly destroyed"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 2
	check_err $? "Did not match on correct filter after the first was removed"

	tp_record "objagg:*" "tc filter del dev $h2 ingress protocol ip \
		   pref 2 handle 102 flower"
	tp_check_hits "objagg:objagg_obj_parent_unassign" 1
	check_err $? "delta was not destroyed"
	tp_check_hits "objagg:objagg_obj_root_destroy" 1
	check_err $? "eRP was not destroyed"

	log_test "delta simple test ($tcflags)"
}

delta_two_masks_one_key_test()
{
	# If 2 keys are the same and only differ in mask in a way that
	# they belong under the same ERP (second is delta of the first),
	# there should be C-TCAM spill.

	RET=0

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	tp_record "mlxsw:*" "tc filter add dev $h2 ingress protocol ip \
		   pref 1 handle 101 flower $tcflags dst_ip 192.0.2.0/24 \
		   action drop"
	tp_check_hits "mlxsw:mlxsw_sp_acl_atcam_entry_add_ctcam_spill" 0
	check_err $? "incorrect C-TCAM spill while inserting the first rule"

	tp_record "mlxsw:*" "tc filter add dev $h2 ingress protocol ip \
		   pref 2 handle 102 flower $tcflags dst_ip 192.0.2.2 \
		   action drop"
	tp_check_hits "mlxsw:mlxsw_sp_acl_atcam_entry_add_ctcam_spill" 1
	check_err $? "C-TCAM spill did not happen while inserting the second rule"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "delta two masks one key test ($tcflags)"
}

delta_simple_rehash_test()
{
	RET=0

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 0
	check_err $? "Failed to set ACL region rehash interval"

	tp_record_all mlxsw:mlxsw_sp_acl_tcam_vregion_rehash 7
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_fail $? "Rehash trace was hit even when rehash should be disabled"

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 3000
	check_err $? "Failed to set ACL region rehash interval"

	sleep 1

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.1.0/25 action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.3.0/24 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 103 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tp_record_all mlxsw:* 3
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_err $? "Rehash trace was not hit"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate
	check_err $? "Migrate trace was not hit"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate_end
	check_err $? "Migrate end trace was not hit"
	tp_record_all mlxsw:* 3
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_err $? "Rehash trace was not hit"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate
	check_fail $? "Migrate trace was hit when no migration should happen"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate_end
	check_fail $? "Migrate end trace was hit when no migration should happen"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter after rehash"

	tc_check_packets "dev $h2 ingress" 103 1
	check_fail $? "Matched a wrong filter after rehash"

	tc_check_packets "dev $h2 ingress" 102 2
	check_err $? "Did not match on correct filter after rehash"

	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	log_test "delta simple rehash test ($tcflags)"
}

delta_simple_ipv6_rehash_test()
{
	RET=0

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 0
	check_err $? "Failed to set ACL region rehash interval"

	tp_record_all mlxsw:mlxsw_sp_acl_tcam_vregion_rehash 7
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_fail $? "Rehash trace was hit even when rehash should be disabled"

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 3000
	check_err $? "Failed to set ACL region rehash interval"

	sleep 1

	tc filter add dev $h2 ingress protocol ipv6 pref 1 handle 101 flower \
		$tcflags dst_ip 2001:db8:1::0/121 action drop
	tc filter add dev $h2 ingress protocol ipv6 pref 2 handle 102 flower \
		$tcflags dst_ip 2001:db8:2::2 action drop
	tc filter add dev $h2 ingress protocol ipv6 pref 3 handle 103 flower \
		$tcflags dst_ip 2001:db8:3::0/120 action drop

	$MZ $h1 -6 -c 1 -p 64 -a $h1mac -b $h2mac \
		-A 2001:db8:2::1 -B 2001:db8:2::2 -t udp -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 103 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	tp_record_all mlxsw:* 3
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_err $? "Rehash trace was not hit"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate
	check_err $? "Migrate trace was not hit"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate_end
	check_err $? "Migrate end trace was not hit"
	tp_record_all mlxsw:* 3
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_err $? "Rehash trace was not hit"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate
	check_fail $? "Migrate trace was hit when no migration should happen"
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_migrate_end
	check_fail $? "Migrate end trace was hit when no migration should happen"

	$MZ $h1 -6 -c 1 -p 64 -a $h1mac -b $h2mac \
		-A 2001:db8:2::1 -B 2001:db8:2::2 -t udp -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter after rehash"

	tc_check_packets "dev $h2 ingress" 103 1
	check_fail $? "Matched a wrong filter after rehash"

	tc_check_packets "dev $h2 ingress" 102 2
	check_err $? "Did not match on correct filter after rehash"

	tc filter del dev $h2 ingress protocol ipv6 pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 1 handle 101 flower

	log_test "delta simple IPv6 rehash test ($tcflags)"
}

TEST_RULE_BASE=256
declare -a test_rules_inserted

test_rule_add()
{
	local iface=$1
	local tcflags=$2
	local index=$3

	if ! [ ${test_rules_inserted[$index]} ] ; then
		test_rules_inserted[$index]=false
	fi
	if ${test_rules_inserted[$index]} ; then
		return
	fi

	local number=$(( $index + $TEST_RULE_BASE ))
	printf -v hexnumber '%x' $number

	batch="${batch}filter add dev $iface ingress protocol ipv6 pref 1 \
		handle $number flower $tcflags \
		src_ip 2001:db8:1::$hexnumber action drop\n"
	test_rules_inserted[$index]=true
}

test_rule_del()
{
	local iface=$1
	local index=$2

	if ! [ ${test_rules_inserted[$index]} ] ; then
		test_rules_inserted[$index]=false
	fi
	if ! ${test_rules_inserted[$index]} ; then
		return
	fi

	local number=$(( $index + $TEST_RULE_BASE ))
	printf -v hexnumber '%x' $number

	batch="${batch}filter del dev $iface ingress protocol ipv6 pref 1 \
		handle $number flower\n"
	test_rules_inserted[$index]=false
}

test_rule_add_or_remove()
{
	local iface=$1
	local tcflags=$2
	local index=$3

	if ! [ ${test_rules_inserted[$index]} ] ; then
		test_rules_inserted[$index]=false
	fi
	if ${test_rules_inserted[$index]} ; then
		test_rule_del $iface $index
	else
		test_rule_add $iface $tcflags $index
	fi
}

test_rule_add_or_remove_random_batch()
{
	local iface=$1
	local tcflags=$2
	local total_count=$3
	local skip=0
	local count=0
	local MAXSKIP=20
	local MAXCOUNT=20

	for ((i=1;i<=total_count;i++)); do
		if (( $skip == 0 )) && (($count == 0)); then
			((skip=$RANDOM % $MAXSKIP + 1))
			((count=$RANDOM % $MAXCOUNT + 1))
		fi
		if (( $skip != 0 )); then
			((skip-=1))
		else
			((count-=1))
			test_rule_add_or_remove $iface $tcflags $i
		fi
	done
}

delta_massive_ipv6_rehash_test()
{
	RET=0

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 0
	check_err $? "Failed to set ACL region rehash interval"

	tp_record_all mlxsw:mlxsw_sp_acl_tcam_vregion_rehash 7
	tp_check_hits_any mlxsw:mlxsw_sp_acl_tcam_vregion_rehash
	check_fail $? "Rehash trace was hit even when rehash should be disabled"

	RANDOM=4432897
	declare batch=""
	test_rule_add_or_remove_random_batch $h2 $tcflags 5000

	echo -n -e $batch | tc -b -

	declare batch=""
	test_rule_add_or_remove_random_batch $h2 $tcflags 5000

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 3000
	check_err $? "Failed to set ACL region rehash interval"

	sleep 1

	tc filter add dev $h2 ingress protocol ipv6 pref 1 handle 101 flower \
		$tcflags dst_ip 2001:db8:1::0/121 action drop
	tc filter add dev $h2 ingress protocol ipv6 pref 2 handle 102 flower \
		$tcflags dst_ip 2001:db8:2::2 action drop
	tc filter add dev $h2 ingress protocol ipv6 pref 3 handle 103 flower \
		$tcflags dst_ip 2001:db8:3::0/120 action drop

	$MZ $h1 -6 -c 1 -p 64 -a $h1mac -b $h2mac \
		-A 2001:db8:2::1 -B 2001:db8:2::2 -t udp -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 103 1
	check_fail $? "Matched a wrong filter"

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Did not match on correct filter"

	echo -n -e $batch | tc -b -

	devlink dev param set $DEVLINK_DEV \
		name acl_region_rehash_interval cmode runtime value 0
	check_err $? "Failed to set ACL region rehash interval"

	$MZ $h1 -6 -c 1 -p 64 -a $h1mac -b $h2mac \
		-A 2001:db8:2::1 -B 2001:db8:2::2 -t udp -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched a wrong filter after rehash"

	tc_check_packets "dev $h2 ingress" 103 1
	check_fail $? "Matched a wrong filter after rehash"

	tc_check_packets "dev $h2 ingress" 102 2
	check_err $? "Did not match on correct filter after rehash"

	tc filter del dev $h2 ingress protocol ipv6 pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ipv6 pref 1 handle 101 flower

	declare batch=""
	for i in {1..5000}; do
		test_rule_del $h2 $tcflags $i
	done
	echo -e $batch | tc -b -

	log_test "delta massive IPv6 rehash test ($tcflags)"
}

bloom_simple_test()
{
	# Bloom filter requires that the eRP table is used. This test
	# verifies that Bloom filter is not harming correctness of ACLs.
	# First, make sure that eRP table is used and then set rule patterns
	# which are distant enough and will result skipping a lookup after
	# consulting the Bloom filter. Although some eRP lookups are skipped,
	# the correct filter should be hit.

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 5 handle 104 flower \
		$tcflags dst_ip 198.51.100.2 action drop
	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.0.0/8 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Two filters - did not match highest priority"

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 198.51.100.1 -B 198.51.100.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 104 1
	check_err $? "Single filter - did not match"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Low prio filter - did not match"

	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 198.0.0.0/8 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 198.51.100.1 -B 198.51.100.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Two filters - did not match highest priority after add"

	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress protocol ip pref 5 handle 104 flower

	log_test "bloom simple test ($tcflags)"
}

bloom_complex_test()
{
	# Bloom filter index computation is affected from region ID, eRP
	# ID and from the region key size. In order to excercise those parts
	# of the Bloom filter code, use a series of regions, each with a
	# different key size and send packet that should hit all of them.
	local index

	RET=0
	NUM_CHAINS=4
	BASE_INDEX=100

	# Create chain with up to 2 key blocks (ip_proto only)
	tc chain add dev $h2 ingress chain 1 protocol ip flower \
		ip_proto tcp &> /dev/null
	# Create chain with 2-4 key blocks (ip_proto, src MAC)
	tc chain add dev $h2 ingress chain 2 protocol ip flower \
		ip_proto tcp \
		src_mac 00:00:00:00:00:00/FF:FF:FF:FF:FF:FF &> /dev/null
	# Create chain with 4-8 key blocks (ip_proto, src & dst MAC, IPv4 dest)
	tc chain add dev $h2 ingress chain 3 protocol ip flower \
		ip_proto tcp \
		dst_mac 00:00:00:00:00:00/FF:FF:FF:FF:FF:FF \
		src_mac 00:00:00:00:00:00/FF:FF:FF:FF:FF:FF \
		dst_ip 0.0.0.0/32 &> /dev/null
	# Default chain contains all fields and therefore is 8-12 key blocks
	tc chain add dev $h2 ingress chain 4

	# We need at least 2 rules in every region to have eRP table active
	# so create a dummy rule per chain using a different pattern
	for i in $(eval echo {0..$NUM_CHAINS}); do
		index=$((BASE_INDEX - 1 - i))
		tc filter add dev $h2 ingress chain $i protocol ip \
			pref 2 handle $index flower \
			$tcflags ip_proto tcp action drop
	done

	# Add rules to test Bloom filter, each in a different chain
	index=$BASE_INDEX
	tc filter add dev $h2 ingress protocol ip \
		pref 1 handle $((++index)) flower \
		$tcflags dst_ip 192.0.0.0/16 action goto chain 1
	tc filter add dev $h2 ingress chain 1 protocol ip \
		pref 1 handle $((++index)) flower \
		$tcflags action goto chain 2
	tc filter add dev $h2 ingress chain 2 protocol ip \
		pref 1 handle $((++index)) flower \
		$tcflags src_mac $h1mac action goto chain 3
	tc filter add dev $h2 ingress chain 3 protocol ip \
		pref 1 handle $((++index)) flower \
		$tcflags dst_ip 192.0.0.0/8 action goto chain 4
	tc filter add dev $h2 ingress chain 4 protocol ip \
		pref 1 handle $((++index)) flower \
		$tcflags src_ip 192.0.2.0/24 action drop

	# Send a packet that is supposed to hit all chains
	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	for i in $(eval echo {0..$NUM_CHAINS}); do
		index=$((BASE_INDEX + i + 1))
		tc_check_packets "dev $h2 ingress" $index 1
		check_err $? "Did not match chain $i"
	done

	# Rules cleanup
	for i in $(eval echo {$NUM_CHAINS..0}); do
		index=$((BASE_INDEX - i - 1))
		tc filter del dev $h2 ingress chain $i \
			pref 2 handle $index flower
		index=$((BASE_INDEX + i + 1))
		tc filter del dev $h2 ingress chain $i \
			pref 1 handle $index flower
	done

	# Chains cleanup
	for i in $(eval echo {$NUM_CHAINS..1}); do
		tc chain del dev $h2 ingress chain $i
	done

	log_test "bloom complex test ($tcflags)"
}


bloom_delta_test()
{
	# When multiple masks are used, the eRP table is activated. When
	# masks are close enough (delta) the masks reside on the same
	# eRP table. This test verifies that the eRP table is correctly
	# allocated and used in delta condition and that Bloom filter is
	# still functional with delta.

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.1.0.0/16 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.1.2.1 -B 192.1.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 103 1
	check_err $? "Single filter - did not match"

	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.2.1.0/24 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.2.1.1 -B 192.2.1.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_err $? "Delta filters - did not match second filter"

	tc filter del dev $h2 ingress protocol ip pref 3 handle 103 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower

	log_test "bloom delta test ($tcflags)"
}

max_erp_entries_test()
{
	# The number of eRP entries is limited. Once the maximum number of eRPs
	# has been reached, filters cannot be added. This test verifies that
	# when this limit is reached, inserstion fails without crashing.

	RET=0

	local num_masks=32
	local num_regions=15
	local chain_failed
	local mask_failed
	local ret

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	for ((i=1; i < $num_regions; i++)); do
		for ((j=$num_masks; j >= 0; j--)); do
			tc filter add dev $h2 ingress chain $i protocol ip \
				pref $i	handle $j flower $tcflags \
				dst_ip 192.1.0.0/$j &> /dev/null
			ret=$?

			if [ $ret -ne 0 ]; then
				chain_failed=$i
				mask_failed=$j
				break 2
			fi
		done
	done

	# We expect to exceed the maximum number of eRP entries, so that
	# insertion eventually fails. Otherwise, the test should be adjusted to
	# add more filters.
	check_fail $ret "expected to exceed number of eRP entries"

	for ((; i >= 1; i--)); do
		for ((j=0; j <= $num_masks; j++)); do
			tc filter del dev $h2 ingress chain $i protocol ip \
				pref $i handle $j flower &> /dev/null
		done
	done

	log_test "max eRP entries test ($tcflags). " \
		"max chain $chain_failed, mask $mask_failed"
}

max_group_size_test()
{
	# The number of ACLs in an ACL group is limited. Once the maximum
	# number of ACLs has been reached, filters cannot be added. This test
	# verifies that when this limit is reached, insertion fails without
	# crashing.

	RET=0

	local num_acls=32
	local max_size
	local ret

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	for ((i=1; i < $num_acls; i++)); do
		if [[ $(( i % 2 )) == 1 ]]; then
			tc filter add dev $h2 ingress pref $i proto ipv4 \
				flower $tcflags dst_ip 198.51.100.1/32 \
				ip_proto tcp tcp_flags 0x01/0x01 \
				action drop &> /dev/null
		else
			tc filter add dev $h2 ingress pref $i proto ipv6 \
				flower $tcflags dst_ip 2001:db8:1::1/128 \
				action drop &> /dev/null
		fi

		ret=$?
		[[ $ret -ne 0 ]] && max_size=$((i - 1)) && break
	done

	# We expect to exceed the maximum number of ACLs in a group, so that
	# insertion eventually fails. Otherwise, the test should be adjusted to
	# add more filters.
	check_fail $ret "expected to exceed number of ACLs in a group"

	for ((; i >= 1; i--)); do
		if [[ $(( i % 2 )) == 1 ]]; then
			tc filter del dev $h2 ingress pref $i proto ipv4 \
				flower $tcflags dst_ip 198.51.100.1/32 \
				ip_proto tcp tcp_flags 0x01/0x01 \
				action drop &> /dev/null
		else
			tc filter del dev $h2 ingress pref $i proto ipv6 \
				flower $tcflags dst_ip 2001:db8:1::1/128 \
				action drop &> /dev/null
		fi
	done

	log_test "max ACL group size test ($tcflags). max size $max_size"
}

collision_test()
{
	# Filters cannot share an eRP if in the common unmasked part (i.e.,
	# without the delta bits) they have the same values. If the driver does
	# not prevent such configuration (by spilling into the C-TCAM), then
	# multiple entries will be present in the device with the same key,
	# leading to collisions and a reduced scale.
	#
	# Create such a scenario and make sure all the filters are successfully
	# added.

	RET=0

	local ret

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	# Add a single dst_ip/24 filter and multiple dst_ip/32 filters that all
	# have the same values in the common unmasked part (dst_ip/24).

	tc filter add dev $h2 ingress pref 1 proto ipv4 handle 101 \
		flower $tcflags dst_ip 198.51.100.0/24 \
		action drop

	for i in {0..255}; do
		tc filter add dev $h2 ingress pref 2 proto ipv4 \
			handle $((102 + i)) \
			flower $tcflags dst_ip 198.51.100.${i}/32 \
			action drop
		ret=$?
		[[ $ret -ne 0 ]] && break
	done

	check_err $ret "failed to add all the filters"

	for i in {255..0}; do
		tc filter del dev $h2 ingress pref 2 proto ipv4 \
			handle $((102 + i)) flower
	done

	tc filter del dev $h2 ingress pref 1 proto ipv4 handle 101 flower

	log_test "collision test ($tcflags)"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}
	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

	vrf_prepare

	h1_create
	h2_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy

	vrf_cleanup
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

if ! tc_offload_check; then
	check_err 1 "Could not test offloaded functionality"
	log_test "mlxsw-specific tests for tc flower"
	exit
else
	tcflags="skip_sw"
	tests_run
fi

exit $EXIT_STATUS

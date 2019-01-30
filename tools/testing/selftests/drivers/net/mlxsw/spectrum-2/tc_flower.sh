#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking the A-TCAM and C-TCAM operation in Spectrum-2.
# It tries to exercise as many code paths in the eRP state machine as
# possible.

lib_dir=$(dirname $0)/../../../../net/forwarding

ALL_TESTS="single_mask_test identical_filters_test two_masks_test \
	multiple_masks_test ctcam_edge_cases_test delta_simple_test \
	delta_two_masks_one_key_test bloom_simple_test \
	bloom_complex_test bloom_delta_test"
NUM_NETIFS=2
source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

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
	# there should be no C-TCAM spill.

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
	tp_check_hits "mlxsw:mlxsw_sp_acl_atcam_entry_add_ctcam_spill" 0
	check_err $? "incorrect C-TCAM spill while inserting the second rule"

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

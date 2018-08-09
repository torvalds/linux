#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test is for checking the A-TCAM and C-TCAM operation in Spectrum-2.
# It tries to exercise as many code paths in the eRP state machine as
# possible.

lib_dir=$(dirname $0)/../../../../net/forwarding

ALL_TESTS="single_mask_test identical_filters_test two_masks_test \
	multiple_masks_test ctcam_edge_cases_test"
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
		$tcflags dst_ip 192.0.0.0/16 action drop

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

	local index

	RET=0

	NUM_MASKS=32
	BASE_INDEX=100

	for i in $(eval echo {1..$NUM_MASKS}); do
		index=$((BASE_INDEX - i))

		tc filter add dev $h2 ingress protocol ip pref $index \
			handle $index \
			flower $tcflags dst_ip 192.0.2.2/${i} src_ip 192.0.2.1 \
			action drop

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
		$tcflags dst_ip 192.0.2.0/24 action drop

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

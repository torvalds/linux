#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

ALL_TESTS="loopback_test"
NUM_NETIFS=2
lib_dir=$(dirname "$0")
source "$lib_dir"/../../../net/forwarding/tc_common.sh
source "$lib_dir"/../../../net/forwarding/lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
	tc qdisc add dev $h1 clsact
}

h1_destroy()
{
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1 192.0.2.1/24
}

h2_create()
{
	simple_if_init $h2
}

h2_destroy()
{
	simple_if_fini $h2
}

loopback_test()
{
	RET=0

	tc filter add dev $h1 ingress protocol arp pref 1 handle 101 flower \
		skip_hw arp_op reply arp_tip 192.0.2.1 action drop

	$MZ $h1 -c 1 -t arp -q

	tc_check_packets "dev $h1 ingress" 101 1
	check_fail $? "Matched on a filter without loopback setup"

	ethtool -K $h1 loopback on
	check_err $? "Failed to enable loopback"

	setup_wait_dev $h1

	$MZ $h1 -c 1 -t arp -q

	tc_check_packets "dev $h1 ingress" 101 1
	check_err $? "Did not match on filter with loopback"

	ethtool -K $h1 loopback off
	check_err $? "Failed to disable loopback"

	$MZ $h1 -c 1 -t arp -q

	tc_check_packets "dev $h1 ingress" 101 2
	check_fail $? "Matched on a filter after loopback was removed"

	tc filter del dev $h1 ingress protocol arp pref 1 handle 101 flower

	log_test "loopback"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}

	vrf_prepare

	h1_create
	h2_create

	if ethtool -k $h1 | grep loopback | grep -q fixed; then
		log_test "SKIP: dev $h1 does not support loopback feature"
		exit $ksft_skip
	fi
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

exit $EXIT_STATUS

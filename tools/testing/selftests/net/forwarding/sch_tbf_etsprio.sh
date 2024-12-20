#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	ping_ipv4
	tbf_test
	tbf_root_test
"
source $lib_dir/sch_tbf_core.sh

QDISC_TYPE=${QDISC% *}

tbf_test_one()
{
	local bs=$1; shift

	tc qdisc replace dev $swp2 parent 10:3 handle 103: tbf \
	   rate 400Mbit burst $bs limit 1M
	tc qdisc replace dev $swp2 parent 10:2 handle 102: tbf \
	   rate 800Mbit burst $bs limit 1M

	do_tbf_test 10 400 $bs
	do_tbf_test 11 800 $bs
}

tbf_test()
{
	log_info "Testing root-$QDISC_TYPE-tbf"

	# This test is used for both ETS and PRIO. Even though we only need two
	# bands, PRIO demands a minimum of three.
	tc qdisc add dev $swp2 root handle 10: $QDISC 3 priomap 2 1 0
	defer tc qdisc del dev $swp2 root

	tbf_test_one 128K
}

tbf_root_test()
{
	local bs=128K

	log_info "Testing root-tbf-$QDISC_TYPE"

	tc qdisc replace dev $swp2 root handle 1: \
		tbf rate 400Mbit burst $bs limit 1M
	defer tc qdisc del dev $swp2 root

	tc qdisc replace dev $swp2 parent 1:1 handle 10: \
		$QDISC 3 priomap 2 1 0
	tc qdisc replace dev $swp2 parent 10:3 handle 103: \
		bfifo limit 1M
	tc qdisc replace dev $swp2 parent 10:2 handle 102: \
		bfifo limit 1M
	tc qdisc replace dev $swp2 parent 10:1 handle 101: \
		bfifo limit 1M

	do_tbf_test 10 400 $bs
	do_tbf_test 11 400 $bs
}

if type -t sch_tbf_pre_hook >/dev/null; then
	sch_tbf_pre_hook
fi

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

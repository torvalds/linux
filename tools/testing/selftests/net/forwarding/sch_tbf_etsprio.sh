#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	ping_ipv4
	tbf_test
"
source $lib_dir/sch_tbf_core.sh

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
	# This test is used for both ETS and PRIO. Even though we only need two
	# bands, PRIO demands a minimum of three.
	tc qdisc add dev $swp2 root handle 10: $QDISC 3 priomap 2 1 0
	tbf_test_one 128K
	tc qdisc del dev $swp2 root
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

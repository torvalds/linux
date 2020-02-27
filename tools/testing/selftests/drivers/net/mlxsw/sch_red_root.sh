#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	ping_ipv4
	ecn_test
	red_test
	mc_backlog_test
"
source sch_red_core.sh

BACKLOG=300000

install_qdisc()
{
	local -a args=("$@")

	tc qdisc add dev $swp3 root handle 108: red \
	   limit 1000000 min $BACKLOG max $((BACKLOG + 1)) \
	   probability 1.0 avpkt 8000 burst 38 "${args[@]}"
	sleep 1
}

uninstall_qdisc()
{
	tc qdisc del dev $swp3 root
}

ecn_test()
{
	install_qdisc ecn
	do_ecn_test 10 $BACKLOG
	uninstall_qdisc
}

red_test()
{
	install_qdisc
	do_red_test 10 $BACKLOG
	uninstall_qdisc
}

mc_backlog_test()
{
	install_qdisc
	# Note that the backlog value here does not correspond to RED
	# configuration, but is arbitrary.
	do_mc_backlog_test 10 $BACKLOG
	uninstall_qdisc
}

trap cleanup EXIT

setup_prepare
setup_wait

bail_on_lldpad
tests_run

exit $EXIT_STATUS

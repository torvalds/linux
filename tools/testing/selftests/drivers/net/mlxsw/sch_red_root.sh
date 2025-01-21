#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	ping_ipv4
	ecn_test
	ecn_test_perband
	ecn_nodrop_test
	red_test
	mc_backlog_test
	red_mirror_test
"
source sch_red_core.sh

BACKLOG=300000

install_qdisc()
{
	local -a args=("$@")

	tc qdisc add dev $swp3 parent 1: handle 108: red \
	   limit 1000000 min $BACKLOG max $((BACKLOG + 1)) \
	   probability 1.0 avpkt 8000 burst 38 "${args[@]}"
	sleep 1
}

uninstall_qdisc()
{
	tc qdisc del dev $swp3 parent 1:
}

ecn_test()
{
	install_qdisc ecn
	defer uninstall_qdisc

	do_ecn_test 10 $BACKLOG
}

ecn_test_perband()
{
	install_qdisc ecn
	defer uninstall_qdisc

	do_ecn_test_perband 10 $BACKLOG
}

ecn_nodrop_test()
{
	install_qdisc ecn nodrop
	defer uninstall_qdisc

	do_ecn_nodrop_test 10 $BACKLOG
}

red_test()
{
	install_qdisc
	defer uninstall_qdisc

	do_red_test 10 $BACKLOG
}

mc_backlog_test()
{
	install_qdisc
	defer uninstall_qdisc

	# Note that the backlog value here does not correspond to RED
	# configuration, but is arbitrary.
	do_mc_backlog_test 10 $BACKLOG
}

red_mirror_test()
{
	install_qdisc qevent early_drop block 10
	defer uninstall_qdisc

	do_drop_mirror_test 10 $BACKLOG
}

bail_on_lldpad "configure DCB" "configure Qdiscs"

trap cleanup EXIT
setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS

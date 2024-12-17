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
	red_trap_test
	ecn_mirror_test
"
: ${QDISC:=ets}
source sch_red_core.sh

# do_ecn_test first build 2/3 of the requested backlog and expects no marking,
# and then builds 3/2 of it and does expect marking. The values of $BACKLOG1 and
# $BACKLOG2 are far enough not to overlap, so that we can assume that if we do
# see (do not see) marking, it is actually due to the configuration of that one
# TC, and not due to configuration of the other TC leaking over.
BACKLOG1=400000
BACKLOG2=1000000

install_root_qdisc()
{
	tc qdisc add dev $swp3 parent 1: handle 10: $QDISC \
	   bands 8 priomap 7 6 5 4 3 2 1 0
}

install_qdisc_tc0()
{
	local -a args=("$@")

	tc qdisc add dev $swp3 parent 10:8 handle 108: red \
	   limit 1000000 min $BACKLOG1 max $((BACKLOG1 + 1)) \
	   probability 1.0 avpkt 8000 burst 51 "${args[@]}"
}

install_qdisc_tc1()
{
	local -a args=("$@")

	tc qdisc add dev $swp3 parent 10:7 handle 107: red \
	   limit 1000000 min $BACKLOG2 max $((BACKLOG2 + 1)) \
	   probability 1.0 avpkt 8000 burst 126 "${args[@]}"
}

install_qdisc()
{
	install_root_qdisc
	install_qdisc_tc0 "$@"
	install_qdisc_tc1 "$@"
	sleep 1
}

uninstall_qdisc_tc0()
{
	tc qdisc del dev $swp3 parent 10:8
}

uninstall_qdisc_tc1()
{
	tc qdisc del dev $swp3 parent 10:7
}

uninstall_root_qdisc()
{
	tc qdisc del dev $swp3 parent 1:
}

uninstall_qdisc()
{
	uninstall_qdisc_tc0
	uninstall_qdisc_tc1
	uninstall_root_qdisc
}

ecn_test()
{
	install_qdisc ecn
	defer uninstall_qdisc

	do_ecn_test 10 $BACKLOG1
	do_ecn_test 11 $BACKLOG2
}

ecn_test_perband()
{
	install_qdisc ecn
	defer uninstall_qdisc

	do_ecn_test_perband 10 $BACKLOG1
	do_ecn_test_perband 11 $BACKLOG2
}

ecn_nodrop_test()
{
	install_qdisc ecn nodrop
	defer uninstall_qdisc

	do_ecn_nodrop_test 10 $BACKLOG1
	do_ecn_nodrop_test 11 $BACKLOG2
}

red_test()
{
	install_qdisc
	defer uninstall_qdisc

	# Make sure that we get the non-zero value if there is any.
	local cur=$(busywait 1100 until_counter_is "> 0" \
			    qdisc_stats_get $swp3 10: .backlog)
	(( cur == 0 ))
	check_err $? "backlog of $cur observed on non-busy qdisc"
	log_test "$QDISC backlog properly cleaned"

	do_red_test 10 $BACKLOG1
	do_red_test 11 $BACKLOG2
}

mc_backlog_test()
{
	install_qdisc
	defer uninstall_qdisc

	# Note that the backlog numbers here do not correspond to RED
	# configuration, but are arbitrary.
	do_mc_backlog_test 10 $BACKLOG1
	do_mc_backlog_test 11 $BACKLOG2
}

red_mirror_test()
{
	install_qdisc qevent early_drop block 10
	defer uninstall_qdisc

	do_drop_mirror_test 10 $BACKLOG1 early_drop
	do_drop_mirror_test 11 $BACKLOG2 early_drop
}

red_trap_test()
{
	install_qdisc qevent early_drop block 10
	defer uninstall_qdisc

	do_drop_trap_test 10 $BACKLOG1 early_drop
	do_drop_trap_test 11 $BACKLOG2 early_drop
}

ecn_mirror_test()
{
	install_qdisc ecn qevent mark block 10
	defer uninstall_qdisc

	do_mark_mirror_test 10 $BACKLOG1
	do_mark_mirror_test 11 $BACKLOG2
}

bail_on_lldpad "configure DCB" "configure Qdiscs"

trap cleanup EXIT
setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS

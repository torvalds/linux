#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# A driver for the ETS selftest that implements testing in slowpath.
lib_dir=.
source sch_ets_core.sh

ALL_TESTS="
	ping_ipv4
	priomap_mode
	ets_test_strict
	ets_test_mixed
	ets_test_dwrr
	ets_test_plug
	classifier_mode
	ets_test_strict
	ets_test_mixed
	ets_test_dwrr
"

switch_create()
{
	ets_switch_create

	# Create a bottleneck so that the DWRR process can kick in.
	tc qdisc add dev $swp2 root handle 1: tbf \
	   rate 1Gbit burst 1Mbit latency 100ms
	defer tc qdisc del dev $swp2 root
	PARENT="parent 1:"
}

# Callback from sch_ets_tests.sh
collect_stats()
{
	local -a streams=("$@")
	local stream

	for stream in ${streams[@]}; do
		qdisc_parent_stats_get $swp2 10:$((stream + 1)) .bytes
	done
}

ets_run

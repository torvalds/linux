#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that policers shared by different tc filters are correctly reference
# counted by observing policers' occupancy via devlink-resource.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	tc_police_occ_test
"
NUM_NETIFS=2
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

h1_create()
{
	simple_if_init $h1
}

h1_destroy()
{
	simple_if_fini $h1
}

switch_create()
{
	simple_if_init $swp1
	tc qdisc add dev $swp1 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp1 clsact
	simple_if_fini $swp1
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	vrf_prepare

	h1_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h1_destroy

	vrf_cleanup
}

tc_police_occ_get()
{
	devlink_resource_occ_get global_policers single_rate_policers
}

tc_police_occ_test()
{
	RET=0

	local occ=$(tc_police_occ_get)

	tc filter add dev $swp1 ingress pref 1 handle 101 proto ip \
		flower skip_sw \
		action police rate 100mbit burst 100k conform-exceed drop/ok
	(( occ + 1 == $(tc_police_occ_get) ))
	check_err $? "Got occupancy $(tc_police_occ_get), expected $((occ + 1))"

	tc filter del dev $swp1 ingress pref 1 handle 101 flower
	(( occ == $(tc_police_occ_get) ))
	check_err $? "Got occupancy $(tc_police_occ_get), expected $occ"

	tc filter add dev $swp1 ingress pref 1 handle 101 proto ip \
		flower skip_sw \
		action police rate 100mbit burst 100k conform-exceed drop/ok \
		index 10
	tc filter add dev $swp1 ingress pref 2 handle 102 proto ip \
		flower skip_sw action police index 10

	(( occ + 1 == $(tc_police_occ_get) ))
	check_err $? "Got occupancy $(tc_police_occ_get), expected $((occ + 1))"

	tc filter del dev $swp1 ingress pref 2 handle 102 flower
	(( occ + 1 == $(tc_police_occ_get) ))
	check_err $? "Got occupancy $(tc_police_occ_get), expected $((occ + 1))"

	tc filter del dev $swp1 ingress pref 1 handle 101 flower
	(( occ == $(tc_police_occ_get) ))
	check_err $? "Got occupancy $(tc_police_occ_get), expected $occ"

	log_test "tc police occupancy"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

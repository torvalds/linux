#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test that filters that match on the same port range, but with different
# combination of IPv4/IPv6 and TCP/UDP all use the same port range register by
# observing port range registers' occupancy via devlink-resource.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	port_range_occ_test
	port_range_occ_tmplt_test
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

port_range_occ_get()
{
	devlink_resource_occ_get port_range_registers
}

port_range_occ_test()
{
	RET=0

	local occ=$(port_range_occ_get)

	# Two port range registers are used, for source and destination port
	# ranges.
	tc filter add dev $swp1 ingress pref 1 handle 101 proto ip \
		flower skip_sw ip_proto udp src_port 1-100 dst_port 1-100 \
		action pass
	(( occ + 2 == $(port_range_occ_get) ))
	check_err $? "Got occupancy $(port_range_occ_get), expected $((occ + 2))"

	tc filter add dev $swp1 ingress pref 1 handle 102 proto ip \
		flower skip_sw ip_proto tcp src_port 1-100 dst_port 1-100 \
		action pass
	tc filter add dev $swp1 ingress pref 2 handle 103 proto ipv6 \
		flower skip_sw ip_proto udp src_port 1-100 dst_port 1-100 \
		action pass
	tc filter add dev $swp1 ingress pref 2 handle 104 proto ipv6 \
		flower skip_sw ip_proto tcp src_port 1-100 dst_port 1-100 \
		action pass
	(( occ + 2 == $(port_range_occ_get) ))
	check_err $? "Got occupancy $(port_range_occ_get), expected $((occ + 2))"

	tc filter del dev $swp1 ingress pref 2 handle 104 flower
	tc filter del dev $swp1 ingress pref 2 handle 103 flower
	tc filter del dev $swp1 ingress pref 1 handle 102 flower
	(( occ + 2 == $(port_range_occ_get) ))
	check_err $? "Got occupancy $(port_range_occ_get), expected $((occ + 2))"

	tc filter del dev $swp1 ingress pref 1 handle 101 flower
	(( occ == $(port_range_occ_get) ))
	check_err $? "Got occupancy $(port_range_occ_get), expected $occ"

	log_test "port range occupancy"
}

port_range_occ_tmplt_test()
{
	local occ

	RET=0
	occ=$(port_range_occ_get)

	# Addition of a chain template should not impact occupancy.
	tc chain add dev "$swp1" ingress chain 1 protocol ip \
		flower ip_proto udp src_port 501-600 dst_port 701-800
	check_err $? "Could not add chain template"

	(( occ == $(port_range_occ_get) ))
	check_err $? "Got occupancy $(port_range_occ_get), expected $occ"

	# And just for completeness sake, neither should deletion.
	tc chain del dev "$swp1" ingress chain 1
	check_err $? "Could not delete chain template"

	(( occ == $(port_range_occ_get) ))
	check_err $? "Got occupancy $(port_range_occ_get), expected $occ"

	log_test "port range occupancy after template create/destroy"
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	shared_block_drop_test
	egress_redirect_test
	multi_mirror_test
"
NUM_NETIFS=2

source $lib_dir/tc_common.sh
source $lib_dir/lib.sh

switch_create()
{
	simple_if_init $swp1 192.0.2.1/24
	simple_if_init $swp2 192.0.2.2/24
}

switch_destroy()
{
	simple_if_fini $swp2 192.0.2.2/24
	simple_if_fini $swp1 192.0.2.1/24
}

shared_block_drop_test()
{
	RET=0

	# It is forbidden in mlxsw driver to have mixed-bound
	# shared block with a drop rule.

	tc qdisc add dev $swp1 ingress_block 22 clsact
	check_err $? "Failed to create clsact with ingress block"

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_err $? "Failed to add drop rule to ingress bound block"

	tc qdisc add dev $swp2 ingress_block 22 clsact
	check_err $? "Failed to create another clsact with ingress shared block"

	tc qdisc del dev $swp2 clsact

	tc qdisc add dev $swp2 egress_block 22 clsact
	check_fail $? "Incorrect success to create another clsact with egress shared block"

	tc filter del block 22 protocol ip pref 1 handle 101 flower

	tc qdisc add dev $swp2 egress_block 22 clsact
	check_err $? "Failed to create another clsact with egress shared block after blocker drop rule removed"

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_fail $? "Incorrect success to add drop rule to mixed bound block"

	tc qdisc del dev $swp1 clsact

	tc qdisc add dev $swp1 egress_block 22 clsact
	check_err $? "Failed to create another clsact with egress shared block"

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_err $? "Failed to add drop rule to egress bound shared block"

	tc filter del block 22 protocol ip pref 1 handle 101 flower

	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

	log_test "shared block drop"
}

egress_redirect_test()
{
	RET=0

	# It is forbidden in mlxsw driver to have mirred redirect on
	# egress-bound block.

	tc qdisc add dev $swp1 ingress_block 22 clsact
	check_err $? "Failed to create clsact with ingress block"

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 \
		action mirred egress redirect dev $swp2
	check_err $? "Failed to add redirect rule to ingress bound block"

	tc qdisc add dev $swp2 ingress_block 22 clsact
	check_err $? "Failed to create another clsact with ingress shared block"

	tc qdisc del dev $swp2 clsact

	tc qdisc add dev $swp2 egress_block 22 clsact
	check_fail $? "Incorrect success to create another clsact with egress shared block"

	tc filter del block 22 protocol ip pref 1 handle 101 flower

	tc qdisc add dev $swp2 egress_block 22 clsact
	check_err $? "Failed to create another clsact with egress shared block after blocker redirect rule removed"

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 \
		action mirred egress redirect dev $swp2
	check_fail $? "Incorrect success to add redirect rule to mixed bound block"

	tc qdisc del dev $swp1 clsact

	tc qdisc add dev $swp1 egress_block 22 clsact
	check_err $? "Failed to create another clsact with egress shared block"

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 \
		action mirred egress redirect dev $swp2
	check_fail $? "Incorrect success to add redirect rule to egress bound shared block"

	tc qdisc del dev $swp2 clsact

	tc filter add block 22 protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 \
		action mirred egress redirect dev $swp2
	check_fail $? "Incorrect success to add redirect rule to egress bound block"

	tc qdisc del dev $swp1 clsact

	log_test "shared block drop"
}

multi_mirror_test()
{
	RET=0

	# It is forbidden in mlxsw driver to have multiple mirror
	# actions in a single rule.

	tc qdisc add dev $swp1 clsact

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 \
		action mirred egress mirror dev $swp2
	check_err $? "Failed to add rule with single mirror action"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 \
		action mirred egress mirror dev $swp2 \
		action mirred egress mirror dev $swp1
	check_fail $? "Incorrect success to add rule with two mirror actions"

	tc qdisc del dev $swp1 clsact

	log_test "multi mirror"
}

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	vrf_prepare

	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy

	vrf_cleanup
}

check_tc_shblock_support

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

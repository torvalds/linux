#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	shared_block_drop_test
	egress_redirect_test
	multi_mirror_test
	matchall_sample_egress_test
	matchall_mirror_behind_flower_ingress_test
	matchall_sample_behind_flower_ingress_test
	matchall_mirror_behind_flower_egress_test
	police_limits_test
	multi_police_test
"
NUM_NETIFS=2

source $lib_dir/tc_common.sh
source $lib_dir/lib.sh
source $lib_dir/devlink_lib.sh

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

matchall_sample_egress_test()
{
	RET=0

	# It is forbidden in mlxsw driver to have matchall with sample action
	# bound on egress. Spectrum-1 specific restriction
	[[ "$DEVLINK_VIDDID" != "15b3:cb84" ]] && return

	tc qdisc add dev $swp1 clsact

	tc filter add dev $swp1 ingress protocol all pref 1 handle 101 \
		matchall skip_sw action sample rate 100 group 1
	check_err $? "Failed to add rule with sample action on ingress"

	tc filter del dev $swp1 ingress protocol all pref 1 handle 101 matchall

	tc filter add dev $swp1 egress protocol all pref 1 handle 101 \
		matchall skip_sw action sample rate 100 group 1
	check_fail $? "Incorrect success to add rule with sample action on egress"

	tc qdisc del dev $swp1 clsact

	log_test "matchall sample egress"
}

matchall_behind_flower_ingress_test()
{
	local action=$1
	local action_args=$2

	RET=0

	# On ingress, all matchall-mirror and matchall-sample
	# rules have to be in front of the flower rules

	tc qdisc add dev $swp1 clsact

	tc filter add dev $swp1 ingress protocol ip pref 10 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop

	tc filter add dev $swp1 ingress protocol all pref 9 handle 102 \
		matchall skip_sw action $action_args
	check_err $? "Failed to add matchall rule in front of a flower rule"

	tc filter del dev $swp1 ingress protocol all pref 9 handle 102 matchall

	tc filter add dev $swp1 ingress protocol all pref 11 handle 102 \
		matchall skip_sw action $action_args
	check_fail $? "Incorrect success to add matchall rule behind a flower rule"

	tc filter del dev $swp1 ingress protocol ip pref 10 handle 101 flower

	tc filter add dev $swp1 ingress protocol all pref 9 handle 102 \
		matchall skip_sw action $action_args

	tc filter add dev $swp1 ingress protocol ip pref 10 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_err $? "Failed to add flower rule behind a matchall rule"

	tc filter del dev $swp1 ingress protocol ip pref 10 handle 101 flower

	tc filter add dev $swp1 ingress protocol ip pref 8 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_fail $? "Incorrect success to add flower rule in front of a matchall rule"

	tc qdisc del dev $swp1 clsact

	log_test "matchall $action flower ingress"
}

matchall_mirror_behind_flower_ingress_test()
{
	matchall_behind_flower_ingress_test "mirror" "mirred egress mirror dev $swp2"
}

matchall_sample_behind_flower_ingress_test()
{
	matchall_behind_flower_ingress_test "sample" "sample rate 100 group 1"
}

matchall_behind_flower_egress_test()
{
	local action=$1
	local action_args=$2

	RET=0

	# On egress, all matchall-mirror rules have to be behind the flower rules

	tc qdisc add dev $swp1 clsact

	tc filter add dev $swp1 egress protocol ip pref 10 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop

	tc filter add dev $swp1 egress protocol all pref 11 handle 102 \
		matchall skip_sw action $action_args
	check_err $? "Failed to add matchall rule in front of a flower rule"

	tc filter del dev $swp1 egress protocol all pref 11 handle 102 matchall

	tc filter add dev $swp1 egress protocol all pref 9 handle 102 \
		matchall skip_sw action $action_args
	check_fail $? "Incorrect success to add matchall rule behind a flower rule"

	tc filter del dev $swp1 egress protocol ip pref 10 handle 101 flower

	tc filter add dev $swp1 egress protocol all pref 11 handle 102 \
		matchall skip_sw action $action_args

	tc filter add dev $swp1 egress protocol ip pref 10 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_err $? "Failed to add flower rule behind a matchall rule"

	tc filter del dev $swp1 egress protocol ip pref 10 handle 101 flower

	tc filter add dev $swp1 egress protocol ip pref 12 handle 101 flower \
		skip_sw dst_ip 192.0.2.2 action drop
	check_fail $? "Incorrect success to add flower rule in front of a matchall rule"

	tc qdisc del dev $swp1 clsact

	log_test "matchall $action flower egress"
}

matchall_mirror_behind_flower_egress_test()
{
	matchall_behind_flower_egress_test "mirror" "mirred egress mirror dev $swp2"
}

police_limits_test()
{
	RET=0

	tc qdisc add dev $swp1 clsact

	tc filter add dev $swp1 ingress pref 1 proto ip handle 101 \
		flower skip_sw \
		action police rate 0.5kbit burst 1m conform-exceed drop/ok
	check_fail $? "Incorrect success to add police action with too low rate"

	tc filter add dev $swp1 ingress pref 1 proto ip handle 101 \
		flower skip_sw \
		action police rate 2.5tbit burst 1g conform-exceed drop/ok
	check_fail $? "Incorrect success to add police action with too high rate"

	tc filter add dev $swp1 ingress pref 1 proto ip handle 101 \
		flower skip_sw \
		action police rate 1.5kbit burst 1m conform-exceed drop/ok
	check_err $? "Failed to add police action with low rate"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	tc filter add dev $swp1 ingress pref 1 proto ip handle 101 \
		flower skip_sw \
		action police rate 1.9tbit burst 1g conform-exceed drop/ok
	check_err $? "Failed to add police action with high rate"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	tc filter add dev $swp1 ingress pref 1 proto ip handle 101 \
		flower skip_sw \
		action police rate 1.5kbit burst 512b conform-exceed drop/ok
	check_fail $? "Incorrect success to add police action with too low burst size"

	tc filter add dev $swp1 ingress pref 1 proto ip handle 101 \
		flower skip_sw \
		action police rate 1.5kbit burst 2k conform-exceed drop/ok
	check_err $? "Failed to add police action with low burst size"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	tc qdisc del dev $swp1 clsact

	log_test "police rate and burst limits"
}

multi_police_test()
{
	RET=0

	# It is forbidden in mlxsw driver to have multiple police
	# actions in a single rule.

	tc qdisc add dev $swp1 clsact

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 \
		flower skip_sw \
		action police rate 100mbit burst 100k conform-exceed drop/ok
	check_err $? "Failed to add rule with single police action"

	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 \
		flower skip_sw \
		action police rate 100mbit burst 100k conform-exceed drop/pipe \
		action police rate 200mbit burst 200k conform-exceed drop/ok
	check_fail $? "Incorrect success to add rule with two police actions"

	tc qdisc del dev $swp1 clsact

	log_test "multi police"
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

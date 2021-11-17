#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="unreachable_chain_test gact_goto_chain_test create_destroy_chain \
	   template_filter_fits"
NUM_NETIFS=2
source tc_common.sh
source lib.sh

tcflags="skip_hw"

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/24
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/24
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/24
}

unreachable_chain_test()
{
	RET=0

	tc filter add dev $h2 ingress chain 1 protocol ip pref 1 handle 1101 \
		flower $tcflags dst_mac $h2mac action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 1101 1
	check_fail $? "matched on filter in unreachable chain"

	tc filter del dev $h2 ingress chain 1 protocol ip pref 1 handle 1101 \
		flower

	log_test "unreachable chain ($tcflags)"
}

gact_goto_chain_test()
{
	RET=0

	tc filter add dev $h2 ingress chain 1 protocol ip pref 1 handle 1101 \
		flower $tcflags dst_mac $h2mac action drop
	tc filter add dev $h2 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_mac $h2mac action drop
	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_mac $h2mac action goto chain 1

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 102 1
	check_fail $? "Matched on a wrong filter"

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match on correct filter with goto chain action"

	tc_check_packets "dev $h2 ingress" 1101 1
	check_err $? "Did not match on correct filter in chain 1"

	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower
	tc filter del dev $h2 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $h2 ingress chain 1 protocol ip pref 1 handle 1101 \
		flower

	log_test "gact goto chain ($tcflags)"
}

create_destroy_chain()
{
	RET=0

	tc chain add dev $h2 ingress
	check_err $? "Failed to create default chain"

	output="$(tc -j chain get dev $h2 ingress)"
	check_err $? "Failed to get default chain"

	echo $output | jq -e ".[] | select(.chain == 0)" &> /dev/null
	check_err $? "Unexpected output for default chain"

	tc chain add dev $h2 ingress chain 1
	check_err $? "Failed to create chain 1"

	output="$(tc -j chain get dev $h2 ingress chain 1)"
	check_err $? "Failed to get chain 1"

	echo $output | jq -e ".[] | select(.chain == 1)" &> /dev/null
	check_err $? "Unexpected output for chain 1"

	output="$(tc -j chain show dev $h2 ingress)"
	check_err $? "Failed to dump chains"

	echo $output | jq -e ".[] | select(.chain == 0)" &> /dev/null
	check_err $? "Can't find default chain in dump"

	echo $output | jq -e ".[] | select(.chain == 1)" &> /dev/null
	check_err $? "Can't find chain 1 in dump"

	tc chain del dev $h2 ingress
	check_err $? "Failed to destroy default chain"

	tc chain del dev $h2 ingress chain 1
	check_err $? "Failed to destroy chain 1"

	log_test "create destroy chain"
}

template_filter_fits()
{
	RET=0

	tc chain add dev $h2 ingress protocol ip \
		flower dst_mac 00:00:00:00:00:00/FF:FF:FF:FF:FF:FF &> /dev/null
	tc chain add dev $h2 ingress chain 1 protocol ip \
		flower src_mac 00:00:00:00:00:00/FF:FF:FF:FF:FF:FF &> /dev/null

	tc filter add dev $h2 ingress protocol ip pref 1 handle 1101 \
		flower dst_mac $h2mac action drop
	check_err $? "Failed to insert filter which fits template"

	tc filter add dev $h2 ingress protocol ip pref 1 handle 1102 \
		flower src_mac $h2mac action drop &> /dev/null
	check_fail $? "Incorrectly succeeded to insert filter which does not template"

	tc filter add dev $h2 ingress chain 1 protocol ip pref 1 handle 1101 \
		flower src_mac $h2mac action drop
	check_err $? "Failed to insert filter which fits template"

	tc filter add dev $h2 ingress chain 1 protocol ip pref 1 handle 1102 \
		flower dst_mac $h2mac action drop &> /dev/null
	check_fail $? "Incorrectly succeeded to insert filter which does not template"

	tc filter del dev $h2 ingress chain 1 protocol ip pref 1 handle 1102 \
		flower &> /dev/null
	tc filter del dev $h2 ingress chain 1 protocol ip pref 1 handle 1101 \
		flower &> /dev/null

	tc filter del dev $h2 ingress protocol ip pref 1 handle 1102 \
		flower &> /dev/null
	tc filter del dev $h2 ingress protocol ip pref 1 handle 1101 \
		flower &> /dev/null

	tc chain del dev $h2 ingress chain 1
	tc chain del dev $h2 ingress

	log_test "template filter fits"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	h2=${NETIFS[p2]}
	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

	vrf_prepare

	h1_create
	h2_create
}

cleanup()
{
	pre_cleanup

	h2_destroy
	h1_destroy

	vrf_cleanup
}

check_tc_chain_support

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_info "Could not test offloaded functionality"
else
	tcflags="skip_sw"
	tests_run
fi

exit $EXIT_STATUS

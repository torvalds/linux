#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

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

trap cleanup EXIT

setup_prepare
setup_wait

unreachable_chain_test
gact_goto_chain_test

tc_offload_check
if [[ $? -ne 0 ]]; then
	log_info "Could not test offloaded functionality"
else
	tcflags="skip_sw"
	unreachable_chain_test
	gact_goto_chain_test
fi

exit $EXIT_STATUS

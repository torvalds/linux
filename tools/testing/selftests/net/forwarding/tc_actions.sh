#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="gact_drop_and_ok_test mirred_egress_redirect_test \
	mirred_egress_mirror_test matchall_mirred_egress_mirror_test \
	gact_trap_test mirred_egress_to_ingress_test \
	mirred_egress_to_ingress_tcp_test"
NUM_NETIFS=4
source tc_common.sh
source lib.sh

require_command ncat

tcflags="skip_hw"

h1_create()
{
	simple_if_init $h1 192.0.2.1/24
	tc qdisc add dev $h1 clsact
}

h1_destroy()
{
	tc qdisc del dev $h1 clsact
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

switch_create()
{
	simple_if_init $swp1 192.0.2.2/24
	tc qdisc add dev $swp1 clsact

	simple_if_init $swp2 192.0.2.1/24
}

switch_destroy()
{
	simple_if_fini $swp2 192.0.2.1/24

	tc qdisc del dev $swp1 clsact
	simple_if_fini $swp1 192.0.2.2/24
}

mirred_egress_test()
{
	local action=$1
	local protocol=$2
	local classifier=$3
	local classifier_args=$4

	RET=0

	tc filter add dev $h2 ingress protocol ip pref 1 handle 101 flower \
		dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_fail $? "Matched without redirect rule inserted"

	tc filter add dev $swp1 ingress protocol $protocol pref 1 handle 101 \
		$classifier $tcflags $classifier_args \
		action mirred egress $action dev $swp2

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $h2 ingress" 101 1
	check_err $? "Did not match incoming $action packet"

	tc filter del dev $swp1 ingress protocol $protocol pref 1 handle 101 \
		$classifier
	tc filter del dev $h2 ingress protocol ip pref 1 handle 101 flower

	log_test "mirred egress $classifier $action ($tcflags)"
}

gact_drop_and_ok_test()
{
	RET=0

	tc filter add dev $swp1 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action drop

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $swp1 ingress" 102 1
	check_err $? "Packet was not dropped"

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		$tcflags dst_ip 192.0.2.2 action ok

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $swp1 ingress" 101 1
	check_err $? "Did not see passed packet"

	tc_check_packets "dev $swp1 ingress" 102 2
	check_fail $? "Packet was dropped and it should not reach here"

	tc filter del dev $swp1 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	log_test "gact drop and ok ($tcflags)"
}

gact_trap_test()
{
	RET=0

	if [[ "$tcflags" != "skip_sw" ]]; then
		return 0;
	fi

	tc filter add dev $swp1 ingress protocol ip pref 1 handle 101 flower \
		skip_hw dst_ip 192.0.2.2 action drop
	tc filter add dev $swp1 ingress protocol ip pref 3 handle 103 flower \
		$tcflags dst_ip 192.0.2.2 action mirred egress redirect \
		dev $swp2

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $swp1 ingress" 101 1
	check_fail $? "Saw packet without trap rule inserted"

	tc filter add dev $swp1 ingress protocol ip pref 2 handle 102 flower \
		$tcflags dst_ip 192.0.2.2 action trap

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t ip -q

	tc_check_packets "dev $swp1 ingress" 102 1
	check_err $? "Packet was not trapped"

	tc_check_packets "dev $swp1 ingress" 101 1
	check_err $? "Did not see trapped packet"

	tc filter del dev $swp1 ingress protocol ip pref 3 handle 103 flower
	tc filter del dev $swp1 ingress protocol ip pref 2 handle 102 flower
	tc filter del dev $swp1 ingress protocol ip pref 1 handle 101 flower

	log_test "trap ($tcflags)"
}

mirred_egress_to_ingress_test()
{
	RET=0

	tc filter add dev $h1 protocol ip pref 100 handle 100 egress flower \
		ip_proto icmp src_ip 192.0.2.1 dst_ip 192.0.2.2 type 8 action \
			ct commit nat src addr 192.0.2.2 pipe \
			ct clear pipe \
			ct commit nat dst addr 192.0.2.1 pipe \
			mirred ingress redirect dev $h1

	tc filter add dev $swp1 protocol ip pref 11 handle 111 ingress flower \
		ip_proto icmp src_ip 192.0.2.1 dst_ip 192.0.2.2 type 8 action drop
	tc filter add dev $swp1 protocol ip pref 12 handle 112 ingress flower \
		ip_proto icmp src_ip 192.0.2.1 dst_ip 192.0.2.2 type 0 action pass

	$MZ $h1 -c 1 -p 64 -a $h1mac -b $h2mac -A 192.0.2.1 -B 192.0.2.2 \
		-t icmp "ping,id=42,seq=10" -q

	tc_check_packets "dev $h1 egress" 100 1
	check_err $? "didn't mirror first packet"

	tc_check_packets "dev $swp1 ingress" 111 1
	check_fail $? "didn't redirect first packet"
	tc_check_packets "dev $swp1 ingress" 112 1
	check_err $? "didn't receive reply to first packet"

	ping 192.0.2.2 -I$h1 -c1 -w1 -q 1>/dev/null 2>&1

	tc_check_packets "dev $h1 egress" 100 2
	check_err $? "didn't mirror second packet"
	tc_check_packets "dev $swp1 ingress" 111 1
	check_fail $? "didn't redirect second packet"
	tc_check_packets "dev $swp1 ingress" 112 2
	check_err $? "didn't receive reply to second packet"

	tc filter del dev $h1 egress protocol ip pref 100 handle 100 flower
	tc filter del dev $swp1 ingress protocol ip pref 11 handle 111 flower
	tc filter del dev $swp1 ingress protocol ip pref 12 handle 112 flower

	log_test "mirred_egress_to_ingress ($tcflags)"
}

mirred_egress_to_ingress_tcp_test()
{
	mirred_e2i_tf1=$(mktemp) mirred_e2i_tf2=$(mktemp)

	RET=0
	dd conv=sparse status=none if=/dev/zero bs=1M count=2 of=$mirred_e2i_tf1
	tc filter add dev $h1 protocol ip pref 100 handle 100 egress flower \
		$tcflags ip_proto tcp src_ip 192.0.2.1 dst_ip 192.0.2.2 \
			action ct commit nat src addr 192.0.2.2 pipe \
			action ct clear pipe \
			action ct commit nat dst addr 192.0.2.1 pipe \
			action ct clear pipe \
			action skbedit ptype host pipe \
			action mirred ingress redirect dev $h1
	tc filter add dev $h1 protocol ip pref 101 handle 101 egress flower \
		$tcflags ip_proto icmp \
			action mirred ingress redirect dev $h1
	tc filter add dev $h1 protocol ip pref 102 handle 102 ingress flower \
		ip_proto icmp \
			action drop

	ip vrf exec v$h1 ncat --recv-only -w10 -l -p 12345 -o $mirred_e2i_tf2 &
	local rpid=$!
	ip vrf exec v$h1 ncat -w1 --send-only 192.0.2.2 12345 <$mirred_e2i_tf1
	wait -n $rpid
	cmp -s $mirred_e2i_tf1 $mirred_e2i_tf2
	check_err $? "server output check failed"

	$MZ $h1 -c 10 -p 64 -a $h1mac -b $h1mac -A 192.0.2.1 -B 192.0.2.1 \
		-t icmp "ping,id=42,seq=5" -q
	tc_check_packets "dev $h1 egress" 101 10
	check_err $? "didn't mirred redirect ICMP"
	tc_check_packets "dev $h1 ingress" 102 10
	check_err $? "didn't drop mirred ICMP"
	local overlimits=$(tc_rule_stats_get ${h1} 101 egress .overlimits)
	test ${overlimits} = 10
	check_err $? "wrong overlimits, expected 10 got ${overlimits}"

	tc filter del dev $h1 egress protocol ip pref 100 handle 100 flower
	tc filter del dev $h1 egress protocol ip pref 101 handle 101 flower
	tc filter del dev $h1 ingress protocol ip pref 102 handle 102 flower

	rm -f $mirred_e2i_tf1 $mirred_e2i_tf2
	log_test "mirred_egress_to_ingress_tcp ($tcflags)"
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	h1mac=$(mac_get $h1)
	h2mac=$(mac_get $h2)

	swp1origmac=$(mac_get $swp1)
	swp2origmac=$(mac_get $swp2)
	ip link set $swp1 address $h2mac
	ip link set $swp2 address $h1mac

	vrf_prepare

	h1_create
	h2_create
	switch_create
}

cleanup()
{
	local tf

	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy

	vrf_cleanup

	ip link set $swp2 address $swp2origmac
	ip link set $swp1 address $swp1origmac

	for tf in $mirred_e2i_tf1 $mirred_e2i_tf2; do rm -f $tf; done
}

mirred_egress_redirect_test()
{
	mirred_egress_test "redirect" "ip" "flower" "dst_ip 192.0.2.2"
}

mirred_egress_mirror_test()
{
	mirred_egress_test "mirror" "ip" "flower" "dst_ip 192.0.2.2"
}

matchall_mirred_egress_mirror_test()
{
	mirred_egress_test "mirror" "all" "matchall" ""
}

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

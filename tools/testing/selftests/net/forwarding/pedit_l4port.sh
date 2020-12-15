#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test sends traffic from H1 to H2. Either on ingress of $swp1, or on egress of $swp2, the
# traffic is acted upon by a pedit action. An ingress filter installed on $h2 verifies that the
# packet looks like expected.
#
# +----------------------+                             +----------------------+
# | H1                   |                             |                   H2 |
# |    + $h1             |                             |            $h2 +     |
# |    | 192.0.2.1/28    |                             |   192.0.2.2/28 |     |
# +----|-----------------+                             +----------------|-----+
#      |                                                                |
# +----|----------------------------------------------------------------|-----+
# | SW |                                                                |     |
# |  +-|----------------------------------------------------------------|-+   |
# |  | + $swp1                       BR                           $swp2 + |   |
# |  +--------------------------------------------------------------------+   |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	test_udp_sport
	test_udp_dport
	test_tcp_sport
	test_tcp_dport
"

NUM_NETIFS=4
source lib.sh
source tc_common.sh

: ${HIT_TIMEOUT:=2000} # ms

h1_create()
{
	simple_if_init $h1 192.0.2.1/28 2001:db8:1::1/64
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28 2001:db8:1::1/64
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28 2001:db8:1::2/64
	tc qdisc add dev $h2 clsact
}

h2_destroy()
{
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/28 2001:db8:1::2/64
}

switch_create()
{
	ip link add name br1 up type bridge vlan_filtering 1
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	tc qdisc add dev $swp1 clsact
	tc qdisc add dev $swp2 clsact
}

switch_destroy()
{
	tc qdisc del dev $swp2 clsact
	tc qdisc del dev $swp1 clsact

	ip link set dev $swp2 nomaster
	ip link set dev $swp1 nomaster
	ip link del dev br1
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	swp2=${NETIFS[p3]}
	h2=${NETIFS[p4]}

	h2mac=$(mac_get $h2)

	vrf_prepare
	h1_create
	h2_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h2_destroy
	h1_destroy
	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 192.0.2.2
}

ping_ipv6()
{
	ping6_test $h1 2001:db8:1::2
}

do_test_pedit_l4port_one()
{
	local pedit_locus=$1; shift
	local pedit_prot=$1; shift
	local pedit_action=$1; shift
	local match_prot=$1; shift
	local match_flower=$1; shift
	local mz_flags=$1; shift
	local saddr=$1; shift
	local daddr=$1; shift

	tc filter add $pedit_locus handle 101 pref 1 \
	   flower action pedit ex munge $pedit_action
	tc filter add dev $h2 ingress handle 101 pref 1 prot $match_prot \
	   flower skip_hw $match_flower action pass

	RET=0

	$MZ $mz_flags $h1 -c 10 -d 20msec -p 100 \
	    -a own -b $h2mac -q -t $pedit_prot sp=54321,dp=12345

	local pkts
	pkts=$(busywait "$TC_HIT_TIMEOUT" until_counter_is ">= 10" \
			tc_rule_handle_stats_get "dev $h2 ingress" 101)
	check_err $? "Expected to get 10 packets, but got $pkts."

	pkts=$(tc_rule_handle_stats_get "$pedit_locus" 101)
	((pkts >= 10))
	check_err $? "Expected to get 10 packets on pedit rule, but got $pkts."

	log_test "$pedit_locus pedit $pedit_action"

	tc filter del dev $h2 ingress pref 1
	tc filter del $pedit_locus pref 1
}

do_test_pedit_l4port()
{
	local locus=$1; shift
	local prot=$1; shift
	local pedit_port=$1; shift
	local flower_port=$1; shift
	local port

	for port in 1 11111 65535; do
		do_test_pedit_l4port_one "$locus" "$prot"			\
					 "$prot $pedit_port set $port"		\
					 ip "ip_proto $prot $flower_port $port"	\
					 "-A 192.0.2.1 -B 192.0.2.2"
	done
}

test_udp_sport()
{
	do_test_pedit_l4port "dev $swp1 ingress" udp sport src_port
	do_test_pedit_l4port "dev $swp2 egress"  udp sport src_port
}

test_udp_dport()
{
	do_test_pedit_l4port "dev $swp1 ingress" udp dport dst_port
	do_test_pedit_l4port "dev $swp2 egress"  udp dport dst_port
}

test_tcp_sport()
{
	do_test_pedit_l4port "dev $swp1 ingress" tcp sport src_port
	do_test_pedit_l4port "dev $swp2 egress"  tcp sport src_port
}

test_tcp_dport()
{
	do_test_pedit_l4port "dev $swp1 ingress" tcp dport dst_port
	do_test_pedit_l4port "dev $swp2 egress"  tcp dport dst_port
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

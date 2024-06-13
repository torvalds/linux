#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test sends traffic from H1 to H2. Either on ingress of $swp1, or on
# egress of $swp2, the traffic is acted upon by a pedit action. An ingress
# filter installed on $h2 verifies that the packet looks like expected.
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
	ping_ipv6
	test_ip4_src
	test_ip4_dst
	test_ip6_src
	test_ip6_dst
"

NUM_NETIFS=4
source lib.sh
source tc_common.sh

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

	ip link set dev $swp2 down
	ip link set dev $swp2 nomaster
	ip link set dev $swp1 down
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

do_test_pedit_ip()
{
	local pedit_locus=$1; shift
	local pedit_action=$1; shift
	local match_prot=$1; shift
	local match_flower=$1; shift
	local mz_flags=$1; shift

	tc filter add $pedit_locus handle 101 pref 1 \
	   flower action pedit ex munge $pedit_action
	tc filter add dev $h2 ingress handle 101 pref 1 prot $match_prot \
	   flower skip_hw $match_flower action pass

	RET=0

	$MZ $mz_flags $h1 -c 10 -d 20msec -p 100 -a own -b $h2mac -q -t ip

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

do_test_pedit_ip6()
{
	local locus=$1; shift
	local pedit_addr=$1; shift
	local flower_addr=$1; shift

	do_test_pedit_ip "$locus" "$pedit_addr set 2001:db8:2::1" ipv6	\
			 "$flower_addr 2001:db8:2::1"			\
			 "-6 -A 2001:db8:1::1 -B 2001:db8:1::2"
}

do_test_pedit_ip4()
{
	local locus=$1; shift
	local pedit_addr=$1; shift
	local flower_addr=$1; shift

	do_test_pedit_ip "$locus" "$pedit_addr set 198.51.100.1" ip	\
			 "$flower_addr 198.51.100.1"			\
			 "-A 192.0.2.1 -B 192.0.2.2"
}

test_ip4_src()
{
	do_test_pedit_ip4 "dev $swp1 ingress" "ip src" src_ip
	do_test_pedit_ip4 "dev $swp2 egress"  "ip src" src_ip
}

test_ip4_dst()
{
	do_test_pedit_ip4 "dev $swp1 ingress" "ip dst" dst_ip
	do_test_pedit_ip4 "dev $swp2 egress"  "ip dst" dst_ip
}

test_ip6_src()
{
	do_test_pedit_ip6 "dev $swp1 ingress" "ip6 src" src_ip
	do_test_pedit_ip6 "dev $swp2 egress"  "ip6 src" src_ip
}

test_ip6_dst()
{
	do_test_pedit_ip6 "dev $swp1 ingress" "ip6 dst" dst_ip
	do_test_pedit_ip6 "dev $swp2 egress"  "ip6 dst" dst_ip
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# This test sends traffic from H1 to H2. Either on ingress of $swp1, or on
# egress of $swp2, the traffic is acted upon by an action skbedit priority. The
# new priority should be taken into account when classifying traffic on the PRIO
# qdisc at $swp2. The test verifies that for different priority values, the
# traffic ends up in expected PRIO band.
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
# |  |                                                             PRIO   |   |
# |  +--------------------------------------------------------------------+   |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	test_ingress
	test_egress
"

NUM_NETIFS=4
source lib.sh

: ${HIT_TIMEOUT:=2000} # ms

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28
}

h2_destroy()
{
	simple_if_fini $h2 192.0.2.2/28
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
	tc qdisc add dev $swp2 root handle 10: \
	   prio bands 8 priomap 7 6 5 4 3 2 1 0
}

switch_destroy()
{
	tc qdisc del dev $swp2 root
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

test_skbedit_priority_one()
{
	local locus=$1; shift
	local prio=$1; shift
	local classid=$1; shift

	RET=0

	tc filter add $locus handle 101 pref 1 \
	   flower action skbedit priority $prio

	local pkt0=$(qdisc_parent_stats_get $swp2 $classid .packets)
	local pkt2=$(tc_rule_handle_stats_get "$locus" 101)
	$MZ $h1 -t udp "sp=54321,dp=12345" -c 10 -d 20msec -p 100 \
	    -a own -b $h2mac -A 192.0.2.1 -B 192.0.2.2 -q

	local pkt1
	pkt1=$(busywait "$HIT_TIMEOUT" until_counter_is ">= $((pkt0 + 10))" \
			qdisc_parent_stats_get $swp2 $classid .packets)
	check_err $? "Expected to get 10 packets on class $classid, but got $((pkt1 - pkt0))."

	local pkt3=$(tc_rule_handle_stats_get "$locus" 101)
	((pkt3 >= pkt2 + 10))
	check_err $? "Expected to get 10 packets on skbedit rule but got $((pkt3 - pkt2))."

	log_test "$locus skbedit priority $prio -> classid $classid"

	tc filter del $locus pref 1
}

test_ingress()
{
	local prio

	for prio in {0..7}; do
		test_skbedit_priority_one "dev $swp1 ingress" \
					  $prio 10:$((8 - prio))
	done
}

test_egress()
{
	local prio

	for prio in {0..7}; do
		test_skbedit_priority_one "dev $swp2 egress" \
					  $prio 10:$((8 - prio))
	done
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

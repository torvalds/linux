#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test IP-in-IP GRE tunnel with key.
# This test uses flat topology for IP tunneling tests. See ip6gre_lib.sh for
# more details.

ALL_TESTS="
	gre_flat
	gre_mtu_change
"

NUM_NETIFS=6
source lib.sh
source ip6gre_lib.sh

setup_prepare()
{
	h1=${NETIFS[p1]}
	ol1=${NETIFS[p2]}

	ul1=${NETIFS[p3]}
	ul2=${NETIFS[p4]}

	ol2=${NETIFS[p5]}
	h2=${NETIFS[p6]}

	forwarding_enable
	vrf_prepare
	h1_create
	h2_create
	sw1_flat_create $ol1 $ul1 key 233
	sw2_flat_create $ol2 $ul2 key 233
}

gre_flat()
{
	test_traffic_ip4ip6 "GRE flat IPv4-in-IPv6 with key"
	test_traffic_ip6ip6 "GRE flat IPv6-in-IPv6 with key"
}

gre_mtu_change()
{
	test_mtu_change
}

cleanup()
{
	pre_cleanup

	sw2_flat_destroy $ol2 $ul2
	sw1_flat_destroy $ol1 $ul1
	h2_destroy
	h1_destroy
	vrf_cleanup
	forwarding_restore
}

trap cleanup EXIT

setup_prepare
setup_wait
tests_run

exit $EXIT_STATUS

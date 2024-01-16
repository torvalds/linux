#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test IP-in-IP GRE tunnels without key.
# This test uses hierarchical topology for IP tunneling tests. See
# ipip_lib.sh for more details.

ALL_TESTS="gre_hier4 gre_mtu_change"

NUM_NETIFS=6
source lib.sh
source ipip_lib.sh

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
	sw1_hierarchical_create gre $ol1 $ul1
	sw2_hierarchical_create gre $ol2 $ul2
}

gre_hier4()
{
	RET=0

	ping_test $h1 192.0.2.18 " gre hierarchical"
}

gre_mtu_change()
{
	test_mtu_change gre
}

cleanup()
{
	pre_cleanup

	sw2_hierarchical_destroy $ol2 $ul2
	sw1_hierarchical_destroy $ol1 $ul1
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

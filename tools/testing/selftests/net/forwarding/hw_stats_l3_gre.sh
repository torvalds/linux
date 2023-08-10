#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test L3 stats on IP-in-IP GRE tunnel without key.

# This test uses flat topology for IP tunneling tests. See ipip_lib.sh for more
# details.

ALL_TESTS="
	ping_ipv4
	test_stats_rx
	test_stats_tx
"
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

	ol1mac=$(mac_get $ol1)

	forwarding_enable
	vrf_prepare
	h1_create
	h2_create
	sw1_flat_create gre $ol1 $ul1
	sw2_flat_create gre $ol2 $ul2
	ip stats set dev g1a l3_stats on
	ip stats set dev g2a l3_stats on
}

cleanup()
{
	pre_cleanup

	ip stats set dev g1a l3_stats off
	ip stats set dev g2a l3_stats off

	sw2_flat_destroy $ol2 $ul2
	sw1_flat_destroy $ol1 $ul1
	h2_destroy
	h1_destroy

	vrf_cleanup
	forwarding_restore
}

ping_ipv4()
{
	RET=0

	ping_test $h1 192.0.2.18 " gre flat"
}

send_packets_ipv4()
{
	# Send 21 packets instead of 20, because the first one might trap and go
	# through the SW datapath, which might not bump the HW counter.
	$MZ $h1 -c 21 -d 20msec -p 100 \
	    -a own -b $ol1mac -A 192.0.2.1 -B 192.0.2.18 \
	    -q -t udp sp=54321,dp=12345
}

test_stats()
{
	local dev=$1; shift
	local dir=$1; shift

	local a
	local b

	RET=0

	a=$(hw_stats_get l3_stats $dev $dir packets)
	send_packets_ipv4
	b=$(busywait "$TC_HIT_TIMEOUT" until_counter_is ">= $a + 20" \
		     hw_stats_get l3_stats $dev $dir packets)
	check_err $? "Traffic not reflected in the counter: $a -> $b"

	log_test "Test $dir packets: $prot"
}

test_stats_tx()
{
	test_stats g1a tx
}

test_stats_rx()
{
	test_stats g2a rx
}

skip_on_veth

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

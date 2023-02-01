#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for DSCP prioritization and rewrite. Packets ingress $swp1 with a DSCP
# tag and are prioritized according to the map at $swp1. They egress $swp2 and
# the DSCP value is updated to match the map at that interface. The updated DSCP
# tag is verified at $h2.
#
# ICMP responses are produced with the same DSCP tag that arrived at $h2. They
# go through prioritization at $swp2 and DSCP retagging at $swp1. The tag is
# verified at $h1--it should match the original tag.
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
# |  |   dcb dscp-prio 10:0...17:7            dcb dscp-prio 20:0...27:7   |   |
# |  +--------------------------------------------------------------------+   |
# +---------------------------------------------------------------------------+

ALL_TESTS="
	ping_ipv4
	test_dscp
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=4
source $lib_dir/lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
	tc qdisc add dev $h1 clsact
	dscp_capture_install $h1 10
}

h1_destroy()
{
	dscp_capture_uninstall $h1 10
	tc qdisc del dev $h1 clsact
	simple_if_fini $h1 192.0.2.1/28
}

h2_create()
{
	simple_if_init $h2 192.0.2.2/28
	tc qdisc add dev $h2 clsact
	dscp_capture_install $h2 20
}

h2_destroy()
{
	dscp_capture_uninstall $h2 20
	tc qdisc del dev $h2 clsact
	simple_if_fini $h2 192.0.2.2/28
}

switch_create()
{
	ip link add name br1 type bridge vlan_filtering 1
	ip link set dev br1 up
	ip link set dev $swp1 master br1
	ip link set dev $swp1 up
	ip link set dev $swp2 master br1
	ip link set dev $swp2 up

	dcb app add dev $swp1 dscp-prio 10:0 11:1 12:2 13:3 14:4 15:5 16:6 17:7
	dcb app add dev $swp2 dscp-prio 20:0 21:1 22:2 23:3 24:4 25:5 26:6 27:7
}

switch_destroy()
{
	dcb app del dev $swp2 dscp-prio 20:0 21:1 22:2 23:3 24:4 25:5 26:6 27:7
	dcb app del dev $swp1 dscp-prio 10:0 11:1 12:2 13:3 14:4 15:5 16:6 17:7

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

dscp_ping_test()
{
	local vrf_name=$1; shift
	local sip=$1; shift
	local dip=$1; shift
	local prio=$1; shift
	local dev_10=$1; shift
	local dev_20=$1; shift
	local key

	local dscp_10=$(((prio + 10) << 2))
	local dscp_20=$(((prio + 20) << 2))

	RET=0

	local -A t0s
	eval "t0s=($(dscp_fetch_stats $dev_10 10)
		   $(dscp_fetch_stats $dev_20 20))"

	local ping_timeout=$((PING_TIMEOUT * 5))
	ip vrf exec $vrf_name \
	   ${PING} -Q $dscp_10 ${sip:+-I $sip} $dip \
		   -c 10 -i 0.5 -w $ping_timeout &> /dev/null

	local -A t1s
	eval "t1s=($(dscp_fetch_stats $dev_10 10)
		   $(dscp_fetch_stats $dev_20 20))"

	for key in ${!t0s[@]}; do
		local expect
		if ((key == prio+10 || key == prio+20)); then
			expect=10
		else
			expect=0
		fi

		local delta=$((t1s[$key] - t0s[$key]))
		((expect == delta))
		check_err $? "DSCP $key: Expected to capture $expect packets, got $delta."
	done

	log_test "DSCP rewrite: $dscp_10-(prio $prio)-$dscp_20"
}

test_dscp()
{
	local prio

	for prio in {0..7}; do
		dscp_ping_test v$h1 192.0.2.1 192.0.2.2 $prio $h1 $h2
	done
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

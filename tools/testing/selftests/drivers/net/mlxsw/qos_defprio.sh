#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for port-default priority. Non-IP packets ingress $swp1 and are
# prioritized according to the default priority specified at the port.
# rx_octets_prio_* counters are used to verify the prioritization.
#
# +----------------------------------+
# | H1                               |
# |    + $h1                         |
# |    | 192.0.2.1/28                |
# +----|-----------------------------+
#      |
# +----|-----------------------------+
# | SW |                             |
# |    + $swp1                       |
# |      192.0.2.2/28                |
# |      dcb app default-prio <prio> |
# +----------------------------------+

ALL_TESTS="
	ping_ipv4
	test_defprio
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=2
: ${HIT_TIMEOUT:=1000} # ms
source $lib_dir/lib.sh

h1_create()
{
	simple_if_init $h1 192.0.2.1/28
}

h1_destroy()
{
	simple_if_fini $h1 192.0.2.1/28
}

switch_create()
{
	ip link set dev $swp1 up
	ip addr add dev $swp1 192.0.2.2/28
}

switch_destroy()
{
	dcb app flush dev $swp1 default-prio
	ip addr del dev $swp1 192.0.2.2/28
	ip link set dev $swp1 down
}

setup_prepare()
{
	h1=${NETIFS[p1]}
	swp1=${NETIFS[p2]}

	vrf_prepare

	h1_create
	switch_create
}

cleanup()
{
	pre_cleanup

	switch_destroy
	h1_destroy

	vrf_cleanup
}

ping_ipv4()
{
	ping_test $h1 192.0.2.2
}

__test_defprio()
{
	local prio_install=$1; shift
	local prio_observe=$1; shift
	local key
	local t1
	local i

	RET=0

	dcb app add dev $swp1 default-prio $prio_install

	local t0=$(ethtool_stats_get $swp1 rx_frames_prio_$prio_observe)
	mausezahn -q $h1 -d 100m -c 10 -t arp reply
	t1=$(busywait "$HIT_TIMEOUT" until_counter_is ">= $((t0 + 10))" \
		ethtool_stats_get $swp1 rx_frames_prio_$prio_observe)

	check_err $? "Default priority $prio_install/$prio_observe: Expected to capture 10 packets, got $((t1 - t0))."
	log_test "Default priority $prio_install/$prio_observe"

	dcb app del dev $swp1 default-prio $prio_install
}

test_defprio()
{
	local prio

	for prio in {0..7}; do
		__test_defprio $prio $prio
	done

	dcb app add dev $swp1 default-prio 3
	__test_defprio 0 3
	__test_defprio 1 3
	__test_defprio 2 3
	__test_defprio 4 4
	__test_defprio 5 5
	__test_defprio 6 6
	__test_defprio 7 7
	dcb app del dev $swp1 default-prio 3
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

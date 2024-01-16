#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Test for port-default priority. Non-IP packets ingress $swp1 and are
# prioritized according to the default priority specified at the port.
# rx_octets_prio_* counters are used to verify the prioritization.
#
# +-----------------------+
# | H1                    |
# |    + $h1              |
# |    | 192.0.2.1/28     |
# +----|------------------+
#      |
# +----|------------------+
# | SW |                  |
# |    + $swp1            |
# |      192.0.2.2/28     |
# |      APP=<prio>,1,0   |
# +-----------------------+

ALL_TESTS="
	ping_ipv4
	test_defprio
"

lib_dir=$(dirname $0)/../../../net/forwarding

NUM_NETIFS=2
: ${HIT_TIMEOUT:=1000} # ms
source $lib_dir/lib.sh

declare -a APP

defprio_install()
{
	local dev=$1; shift
	local prio=$1; shift
	local app="app=$prio,1,0"

	lldptool -T -i $dev -V APP $app >/dev/null
	lldpad_app_wait_set $dev
	APP[$prio]=$app
}

defprio_uninstall()
{
	local dev=$1; shift
	local prio=$1; shift
	local app=${APP[$prio]}

	lldptool -T -i $dev -V APP -d $app >/dev/null
	lldpad_app_wait_del
	unset APP[$prio]
}

defprio_flush()
{
	local dev=$1; shift
	local prio

	if ((${#APP[@]})); then
		lldptool -T -i $dev -V APP -d ${APP[@]} >/dev/null
	fi
	lldpad_app_wait_del
	APP=()
}

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
	defprio_flush $swp1
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

	defprio_install $swp1 $prio_install

	local t0=$(ethtool_stats_get $swp1 rx_frames_prio_$prio_observe)
	mausezahn -q $h1 -d 100m -c 10 -t arp reply
	t1=$(busywait "$HIT_TIMEOUT" until_counter_is ">= $((t0 + 10))" \
		ethtool_stats_get $swp1 rx_frames_prio_$prio_observe)

	check_err $? "Default priority $prio_install/$prio_observe: Expected to capture 10 packets, got $((t1 - t0))."
	log_test "Default priority $prio_install/$prio_observe"

	defprio_uninstall $swp1 $prio_install
}

test_defprio()
{
	local prio

	for prio in {0..7}; do
		__test_defprio $prio $prio
	done

	defprio_install $swp1 3
	__test_defprio 0 3
	__test_defprio 1 3
	__test_defprio 2 3
	__test_defprio 4 4
	__test_defprio 5 5
	__test_defprio 6 6
	__test_defprio 7 7
	defprio_uninstall $swp1 3
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

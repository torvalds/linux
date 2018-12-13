#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test various interface configuration scenarios. Observe that configurations
# deemed valid by mlxsw succeed, invalid configurations fail and that no traces
# are produced. To prevent the test from passing in case traces are produced,
# the user can set the 'kernel.panic_on_warn' and 'kernel.panic_on_oops'
# sysctls in its environment.

lib_dir=$(dirname $0)/../../../net/forwarding

ALL_TESTS="
	rif_set_addr_test
"
NUM_NETIFS=2
source $lib_dir/lib.sh

setup_prepare()
{
	swp1=${NETIFS[p1]}
	swp2=${NETIFS[p2]}

	ip link set dev $swp1 up
	ip link set dev $swp2 up
}

cleanup()
{
	pre_cleanup

	ip link set dev $swp2 down
	ip link set dev $swp1 down
}

rif_set_addr_test()
{
	local swp1_mac=$(mac_get $swp1)
	local swp2_mac=$(mac_get $swp2)

	RET=0

	# $swp1 and $swp2 likely got their IPv6 local addresses already, but
	# here we need to test the transition to RIF.
	ip addr flush dev $swp1
	ip addr flush dev $swp2
	sleep .1

	ip addr add dev $swp1 192.0.2.1/28
	check_err $?

	ip link set dev $swp1 addr 00:11:22:33:44:55
	check_err $?

	# IP address enablement should be rejected if the MAC address prefix
	# doesn't match other RIFs.
	ip addr add dev $swp2 192.0.2.2/28 &>/dev/null
	check_fail $? "IP address addition passed for a device with a wrong MAC"
	ip addr add dev $swp2 192.0.2.2/28 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "no extack for IP address addition"

	ip link set dev $swp2 addr 00:11:22:33:44:66
	check_err $?
	ip addr add dev $swp2 192.0.2.2/28 &>/dev/null
	check_err $?

	# Change of MAC address of a RIF should be forbidden if the new MAC
	# doesn't share the prefix with other MAC addresses.
	ip link set dev $swp2 addr 00:11:22:33:00:66 &>/dev/null
	check_fail $? "change of MAC address passed for a wrong MAC"
	ip link set dev $swp2 addr 00:11:22:33:00:66 2>&1 >/dev/null \
	    | grep -q mlxsw_spectrum
	check_err $? "no extack for MAC address change"

	log_test "RIF - bad MAC change"

	ip addr del dev $swp2 192.0.2.2/28
	ip addr del dev $swp1 192.0.2.1/28

	ip link set dev $swp2 addr $swp2_mac
	ip link set dev $swp1 addr $swp1_mac
}

trap cleanup EXIT

setup_prepare
setup_wait

tests_run

exit $EXIT_STATUS

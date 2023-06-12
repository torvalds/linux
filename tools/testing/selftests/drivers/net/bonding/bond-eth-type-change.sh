#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Test bond device ether type changing
#

ALL_TESTS="
	bond_test_unsuccessful_enslave_type_change
	bond_test_successful_enslave_type_change
"
REQUIRE_MZ=no
NUM_NETIFS=0
lib_dir=$(dirname "$0")
source "$lib_dir"/net_forwarding_lib.sh

bond_check_flags()
{
	local bonddev=$1

	ip -d l sh dev "$bonddev" | grep -q "MASTER"
	check_err $? "MASTER flag is missing from the bond device"

	ip -d l sh dev "$bonddev" | grep -q "SLAVE"
	check_err $? "SLAVE flag is missing from the bond device"
}

# test enslaved bond dev type change from ARPHRD_ETHER and back
# this allows us to test both MASTER and SLAVE flags at once
bond_test_enslave_type_change()
{
	local test_success=$1
	local devbond0="test-bond0"
	local devbond1="test-bond1"
	local devbond2="test-bond2"
	local nonethdev="test-noneth0"

	# create a non-ARPHRD_ETHER device for testing (e.g. nlmon type)
	ip link add name "$nonethdev" type nlmon
	check_err $? "could not create a non-ARPHRD_ETHER device (nlmon)"
	ip link add name "$devbond0" type bond
	if [ $test_success -eq 1 ]; then
		# we need devbond0 in active-backup mode to successfully enslave nonethdev
		ip link set dev "$devbond0" type bond mode active-backup
		check_err $? "could not change bond mode to active-backup"
	fi
	ip link add name "$devbond1" type bond
	ip link add name "$devbond2" type bond
	ip link set dev "$devbond0" master "$devbond1"
	check_err $? "could not enslave $devbond0 to $devbond1"
	# change bond type to non-ARPHRD_ETHER
	ip link set dev "$nonethdev" master "$devbond0" 1>/dev/null 2>/dev/null
	ip link set dev "$nonethdev" nomaster 1>/dev/null 2>/dev/null
	# restore ARPHRD_ETHER type by enslaving such device
	ip link set dev "$devbond2" master "$devbond0"
	check_err $? "could not enslave $devbond2 to $devbond0"
	ip link set dev "$devbond1" nomaster

	bond_check_flags "$devbond0"

	# clean up
	ip link del dev "$devbond0"
	ip link del dev "$devbond1"
	ip link del dev "$devbond2"
	ip link del dev "$nonethdev"
}

bond_test_unsuccessful_enslave_type_change()
{
	RET=0

	bond_test_enslave_type_change 0
	log_test "Change ether type of an enslaved bond device with unsuccessful enslave"
}

bond_test_successful_enslave_type_change()
{
	RET=0

	bond_test_enslave_type_change 1
	log_test "Change ether type of an enslaved bond device with successful enslave"
}

tests_run

exit "$EXIT_STATUS"

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 2020 Intel Corporation.

ksft_pass=0
ksft_fail=1
ksft_xfail=2
ksft_xpass=3
ksft_skip=4

XSKOBJ=xskxceiver

validate_root_exec()
{
	msg="skip all tests:"
	if [ $UID != 0 ]; then
		echo $msg must be run as root >&2
		test_exit $ksft_fail
	else
		return $ksft_pass
	fi
}

validate_veth_support()
{
	msg="skip all tests:"
	if [ $(ip link add $1 type veth 2>/dev/null; echo $?;) != 0 ]; then
		echo $msg veth kernel support not available >&2
		test_exit $ksft_skip
	else
		ip link del $1
		return $ksft_pass
	fi
}

test_status()
{
	statusval=$1
	if [ $statusval -eq $ksft_fail ]; then
		echo "$2: [ FAIL ]"
	elif [ $statusval -eq $ksft_skip ]; then
		echo "$2: [ SKIPPED ]"
	elif [ $statusval -eq $ksft_pass ]; then
		echo "$2: [ PASS ]"
	fi
}

test_exit()
{
	if [ $1 -ne 0 ]; then
		test_status $1 $(basename $0)
	fi
	exit 1
}

cleanup_iface()
{
	ip link set $1 mtu $2
	ip link set $1 xdp off
	ip link set $1 xdpgeneric off
}

clear_configs()
{
	[ $(ip link show $1 &>/dev/null; echo $?;) == 0 ] &&
		{ ip link del $1; }
}

cleanup_exit()
{
	clear_configs $1 $2
}

validate_ip_utility()
{
	[ ! $(type -P ip) ] && { echo "'ip' not found. Skipping tests."; test_exit $ksft_skip; }
}

exec_xskxceiver()
{
        if [[ $busy_poll -eq 1 ]]; then
	        ARGS+="-b "
	fi

	./${XSKOBJ} -i ${VETH0} -i ${VETH1} ${ARGS}
	retval=$?

	if [[ $list -ne 1 ]]; then
	    test_status $retval "${TEST_NAME}"
	    statusList+=($retval)
	    nameList+=(${TEST_NAME})
	fi
}

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

lib_dir=$(dirname $0)/forwarding

ALL_TESTS="altnames_test"
NUM_NETIFS=0
source $lib_dir/lib.sh

DUMMY_DEV=dummytest
SHORT_NAME=shortname
LONG_NAME=someveryveryveryveryveryverylongname

altnames_test()
{
	RET=0
	local output
	local name

	ip link property add $DUMMY_DEV altname $SHORT_NAME
	check_err $? "Failed to add short alternative name"

	output=$(ip -j -p link show $SHORT_NAME)
	check_err $? "Failed to do link show with short alternative name"

	name=$(echo $output | jq -e -r ".[0].altnames[0]")
	check_err $? "Failed to get short alternative name from link show JSON"

	[ "$name" == "$SHORT_NAME" ]
	check_err $? "Got unexpected short alternative name from link show JSON"

	ip -j -p link show $DUMMY_DEV &>/dev/null
	check_err $? "Failed to do link show with original name"

	ip link property add $DUMMY_DEV altname $LONG_NAME
	check_err $? "Failed to add long alternative name"

	output=$(ip -j -p link show $LONG_NAME)
	check_err $? "Failed to do link show with long alternative name"

	name=$(echo $output | jq -e -r ".[0].altnames[1]")
	check_err $? "Failed to get long alternative name from link show JSON"

	[ "$name" == "$LONG_NAME" ]
	check_err $? "Got unexpected long alternative name from link show JSON"

	ip link property del $DUMMY_DEV altname $SHORT_NAME
	check_err $? "Failed to add short alternative name"

	ip -j -p link show $SHORT_NAME &>/dev/null
	check_fail $? "Unexpected success while trying to do link show with deleted short alternative name"

	# long name is left there on purpose to be removed alongside the device

	log_test "altnames test"
}

setup_prepare()
{
	ip link add name $DUMMY_DEV type dummy
}

cleanup()
{
	pre_cleanup
	ip link del name $DUMMY_DEV
}

trap cleanup EXIT

setup_prepare

tests_run

exit $EXIT_STATUS

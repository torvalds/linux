#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ALL_TESTS="
	test_raw_filter
"

net_dir=$(dirname $0)/..
source $net_dir/lib.sh

export CANIF=${CANIF:-"vcan0"}
BITRATE=${BITRATE:-500000}

setup()
{
	if [[ $CANIF == vcan* ]]; then
		ip link add name $CANIF type vcan || exit $ksft_skip
	else
		ip link set dev $CANIF type can bitrate $BITRATE || exit $ksft_skip
	fi
	ip link set dev $CANIF up
	pwd
}

cleanup()
{
	ip link set dev $CANIF down
	if [[ $CANIF == vcan* ]]; then
		ip link delete $CANIF
	fi
}

test_raw_filter()
{
	./test_raw_filter
	check_err $?
	log_test "test_raw_filter"
}

trap cleanup EXIT
setup

tests_run

exit $EXIT_STATUS

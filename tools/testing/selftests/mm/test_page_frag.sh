#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2024 Yunsheng Lin <linyunsheng@huawei.com>
# Copyright (C) 2018 Uladzislau Rezki (Sony) <urezki@gmail.com>
#
# This is a test script for the kernel test driver to test the
# correctness and performance of page_frag's implementation.
# Therefore it is just a kernel module loader. You can specify
# and pass different parameters in order to:
#     a) analyse performance of page fragment allocations;
#     b) stressing and stability check of page_frag subsystem.

DRIVER="./page_frag/page_frag_test.ko"
CPU_LIST=$(grep -m 2 processor /proc/cpuinfo | cut -d ' ' -f 2)
TEST_CPU_0=$(echo $CPU_LIST | awk '{print $1}')

if [ $(echo $CPU_LIST | wc -w) -gt 1 ]; then
	TEST_CPU_1=$(echo $CPU_LIST | awk '{print $2}')
	NR_TEST=100000000
else
	TEST_CPU_1=$TEST_CPU_0
	NR_TEST=1000000
fi

# 1 if fails
exitcode=1

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

check_test_failed_prefix() {
	if dmesg | grep -q 'page_frag_test failed:';then
		echo "page_frag_test failed, please check dmesg"
		exit $exitcode
	fi
}

#
# Static templates for testing of page_frag APIs.
# Also it is possible to pass any supported parameters manually.
#
SMOKE_PARAM="test_push_cpu=$TEST_CPU_0 test_pop_cpu=$TEST_CPU_1"
NONALIGNED_PARAM="$SMOKE_PARAM test_alloc_len=75 nr_test=$NR_TEST"
ALIGNED_PARAM="$NONALIGNED_PARAM test_align=1"

check_test_requirements()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo "$0: Must be run as root"
		exit $ksft_skip
	fi

	if ! which insmod > /dev/null 2>&1; then
		echo "$0: You need insmod installed"
		exit $ksft_skip
	fi

	if [ ! -f $DRIVER ]; then
		echo "$0: You need to compile page_frag_test module"
		exit $ksft_skip
	fi
}

run_nonaligned_check()
{
	echo "Run performance tests to evaluate how fast nonaligned alloc API is."

	insmod $DRIVER $NONALIGNED_PARAM > /dev/null 2>&1
}

run_aligned_check()
{
	echo "Run performance tests to evaluate how fast aligned alloc API is."

	insmod $DRIVER $ALIGNED_PARAM > /dev/null 2>&1
}

run_smoke_check()
{
	echo "Run smoke test."

	insmod $DRIVER $SMOKE_PARAM > /dev/null 2>&1
}

usage()
{
	echo -n "Usage: $0 [ aligned ] | [ nonaligned ] | | [ smoke ] | "
	echo "manual parameters"
	echo
	echo "Valid tests and parameters:"
	echo
	modinfo $DRIVER
	echo
	echo "Example usage:"
	echo
	echo "# Shows help message"
	echo "$0"
	echo
	echo "# Smoke testing"
	echo "$0 smoke"
	echo
	echo "# Performance testing for nonaligned alloc API"
	echo "$0 nonaligned"
	echo
	echo "# Performance testing for aligned alloc API"
	echo "$0 aligned"
	echo
	exit 0
}

function validate_passed_args()
{
	VALID_ARGS=`modinfo $DRIVER | awk '/parm:/ {print $2}' | sed 's/:.*//'`

	#
	# Something has been passed, check it.
	#
	for passed_arg in $@; do
		key=${passed_arg//=*/}
		valid=0

		for valid_arg in $VALID_ARGS; do
			if [[ $key = $valid_arg ]]; then
				valid=1
				break
			fi
		done

		if [[ $valid -ne 1 ]]; then
			echo "Error: key is not correct: ${key}"
			exit $exitcode
		fi
	done
}

function run_manual_check()
{
	#
	# Validate passed parameters. If there is wrong one,
	# the script exists and does not execute further.
	#
	validate_passed_args $@

	echo "Run the test with following parameters: $@"
	insmod $DRIVER $@ > /dev/null 2>&1
}

function run_test()
{
	if [ $# -eq 0 ]; then
		usage
	else
		if [[ "$1" = "smoke" ]]; then
			run_smoke_check
		elif [[ "$1" = "nonaligned" ]]; then
			run_nonaligned_check
		elif [[ "$1" = "aligned" ]]; then
			run_aligned_check
		else
			run_manual_check $@
		fi
	fi

	check_test_failed_prefix

	echo "Done."
	echo "Check the kernel ring buffer to see the summary."
}

check_test_requirements
run_test $@

exit 0

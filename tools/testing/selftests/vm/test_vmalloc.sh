#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2018 Uladzislau Rezki (Sony) <urezki@gmail.com>
#
# This is a test script for the kernel test driver to analyse vmalloc
# allocator. Therefore it is just a kernel module loader. You can specify
# and pass different parameters in order to:
#     a) analyse performance of vmalloc allocations;
#     b) stressing and stability check of vmalloc subsystem.

TEST_NAME="vmalloc"
DRIVER="test_${TEST_NAME}"

# 1 if fails
exitcode=1

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

#
# Static templates for performance, stressing and smoke tests.
# Also it is possible to pass any supported parameters manualy.
#
PERF_PARAM="single_cpu_test=1 sequential_test_order=1 test_repeat_count=3"
SMOKE_PARAM="single_cpu_test=1 test_loop_count=10000 test_repeat_count=10"
STRESS_PARAM="test_repeat_count=20"

check_test_requirements()
{
	uid=$(id -u)
	if [ $uid -ne 0 ]; then
		echo "$0: Must be run as root"
		exit $ksft_skip
	fi

	if ! which modprobe > /dev/null 2>&1; then
		echo "$0: You need modprobe installed"
		exit $ksft_skip
	fi

	if ! modinfo $DRIVER > /dev/null 2>&1; then
		echo "$0: You must have the following enabled in your kernel:"
		echo "CONFIG_TEST_VMALLOC=m"
		exit $ksft_skip
	fi
}

run_perfformance_check()
{
	echo "Run performance tests to evaluate how fast vmalloc allocation is."
	echo "It runs all test cases on one single CPU with sequential order."

	modprobe $DRIVER $PERF_PARAM > /dev/null 2>&1
	echo "Done."
	echo "Ccheck the kernel message buffer to see the summary."
}

run_stability_check()
{
	echo "Run stability tests. In order to stress vmalloc subsystem we run"
	echo "all available test cases on all available CPUs simultaneously."
	echo "It will take time, so be patient."

	modprobe $DRIVER $STRESS_PARAM > /dev/null 2>&1
	echo "Done."
	echo "Check the kernel ring buffer to see the summary."
}

run_smoke_check()
{
	echo "Run smoke test. Note, this test provides basic coverage."
	echo "Please check $0 output how it can be used"
	echo "for deep performance analysis as well as stress testing."

	modprobe $DRIVER $SMOKE_PARAM > /dev/null 2>&1
	echo "Done."
	echo "Check the kernel ring buffer to see the summary."
}

usage()
{
	echo -n "Usage: $0 [ performance ] | [ stress ] | | [ smoke ] | "
	echo "manual parameters"
	echo
	echo "Valid tests and parameters:"
	echo
	modinfo $DRIVER
	echo
	echo "Example usage:"
	echo
	echo "# Shows help message"
	echo "./${DRIVER}.sh"
	echo
	echo "# Runs 1 test(id_1), repeats it 5 times on all online CPUs"
	echo "./${DRIVER}.sh run_test_mask=1 test_repeat_count=5"
	echo
	echo -n "# Runs 4 tests(id_1|id_2|id_4|id_16) on one CPU with "
	echo "sequential order"
	echo -n "./${DRIVER}.sh single_cpu_test=1 sequential_test_order=1 "
	echo "run_test_mask=23"
	echo
	echo -n "# Runs all tests on all online CPUs, shuffled order, repeats "
	echo "20 times"
	echo "./${DRIVER}.sh test_repeat_count=20"
	echo
	echo "# Performance analysis"
	echo "./${DRIVER}.sh performance"
	echo
	echo "# Stress testing"
	echo "./${DRIVER}.sh stress"
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
		val="${passed_arg:$((${#key}+1))}"
		valid=0

		for valid_arg in $VALID_ARGS; do
			if [[ $key = $valid_arg ]] && [[ $val -gt 0 ]]; then
				valid=1
				break
			fi
		done

		if [[ $valid -ne 1 ]]; then
			echo "Error: key or value is not correct: ${key} $val"
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
	modprobe $DRIVER $@ > /dev/null 2>&1
	echo "Done."
	echo "Check the kernel ring buffer to see the summary."
}

function run_test()
{
	if [ $# -eq 0 ]; then
		usage
	else
		if [[ "$1" = "performance" ]]; then
			run_perfformance_check
		elif [[ "$1" = "stress" ]]; then
			run_stability_check
		elif [[ "$1" = "smoke" ]]; then
			run_smoke_check
		else
			run_manual_check $@
		fi
	fi
}

check_test_requirements
run_test $@

exit 0

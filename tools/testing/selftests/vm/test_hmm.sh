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

TEST_NAME="test_hmm"
DRIVER="test_hmm"

# 1 if fails
exitcode=1

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

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
		echo "CONFIG_TEST_HMM=m"
		exit $ksft_skip
	fi
}

load_driver()
{
	modprobe $DRIVER > /dev/null 2>&1
	if [ $? == 0 ]; then
		major=$(awk "\$2==\"HMM_DMIRROR\" {print \$1}" /proc/devices)
		mknod /dev/hmm_dmirror0 c $major 0
		mknod /dev/hmm_dmirror1 c $major 1
	fi
}

unload_driver()
{
	modprobe -r $DRIVER > /dev/null 2>&1
	rm -f /dev/hmm_dmirror?
}

run_smoke()
{
	echo "Running smoke test. Note, this test provides basic coverage."

	load_driver
	$(dirname "${BASH_SOURCE[0]}")/hmm-tests
	unload_driver
}

usage()
{
	echo -n "Usage: $0"
	echo
	echo "Example usage:"
	echo
	echo "# Shows help message"
	echo "./${TEST_NAME}.sh"
	echo
	echo "# Smoke testing"
	echo "./${TEST_NAME}.sh smoke"
	echo
	exit 0
}

function run_test()
{
	if [ $# -eq 0 ]; then
		usage
	else
		if [ "$1" = "smoke" ]; then
			run_smoke
		else
			usage
		fi
	fi
}

check_test_requirements
run_test $@

exit 0

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
#
# Load kernel module for FPU tests

uid=$(id -u)
if [ $uid -ne 0 ]; then
	echo "$0: Must be run as root"
	exit 1
fi

if ! which modprobe > /dev/null 2>&1; then
	echo "$0: You need modprobe installed"
        exit 4
fi

if ! modinfo test_fpu > /dev/null 2>&1; then
	echo "$0: You must have the following enabled in your kernel:"
	echo "CONFIG_TEST_FPU=m"
	exit 4
fi

NR_CPUS=$(getconf _NPROCESSORS_ONLN)
if [ ! $NR_CPUS ]; then
	NR_CPUS=1
fi

modprobe test_fpu

if [ ! -e /sys/kernel/debug/selftest_helpers/test_fpu ]; then
	mount -t debugfs none /sys/kernel/debug

	if [ ! -e /sys/kernel/debug/selftest_helpers/test_fpu ]; then
		echo "$0: Error mounting debugfs"
		exit 4
	fi
fi

echo "Running 1000 iterations on all CPUs... "
for i in $(seq 1 1000); do
	for c in $(seq 1 $NR_CPUS); do
		./test_fpu &
	done
done

rmmod test_fpu

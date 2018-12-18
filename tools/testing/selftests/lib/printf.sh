#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Runs printf infrastructure using test_printf kernel module

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

if ! /sbin/modprobe -q -n test_printf; then
	echo "printf: module test_printf is not found [SKIP]"
	exit $ksft_skip
fi

if /sbin/modprobe -q test_printf; then
	/sbin/modprobe -q -r test_printf
	echo "printf: ok"
else
	echo "printf: [FAIL]"
	exit 1
fi

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# Runs bitmap infrastructure tests using test_bitmap kernel module
if ! /sbin/modprobe -q -n test_bitmap; then
	echo "bitmap: module test_bitmap is not found [SKIP]"
	exit $ksft_skip
fi

if /sbin/modprobe -q test_bitmap; then
	/sbin/modprobe -q -r test_bitmap
	echo "bitmap: ok"
else
	echo "bitmap: [FAIL]"
	exit 1
fi

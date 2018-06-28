#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

# Kselftest framework requirement - SKIP code is 4.
ksft_skip=4

# Runs API tests for struct ww_mutex (Wait/Wound mutexes)
if ! /sbin/modprobe -q -n test-ww_mutex; then
	echo "ww_mutex: module test-ww_mutex is not found [SKIP]"
	exit $ksft_skip
fi

if /sbin/modprobe -q test-ww_mutex; then
       /sbin/modprobe -q -r test-ww_mutex
       echo "locking/ww_mutex: ok"
else
       echo "locking/ww_mutex: [FAIL]"
       exit 1
fi

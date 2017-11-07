#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Runs API tests for struct ww_mutex (Wait/Wound mutexes)

if /sbin/modprobe -q test-ww_mutex; then
       /sbin/modprobe -q -r test-ww_mutex
       echo "locking/ww_mutex: ok"
else
       echo "locking/ww_mutex: [FAIL]"
       exit 1
fi

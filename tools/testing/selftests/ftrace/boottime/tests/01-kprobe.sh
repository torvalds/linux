#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check kprobe bootconfig settings on tracefs
TRACEDIR="/sys/kernel/tracing"

if [ -f /proc/bootconfig ] && grep -q "dump_bconf" /proc/cmdline 2>/dev/null; then
	echo "=== /proc/bootconfig ==="
	cat /proc/bootconfig
	echo "========================"
fi

if [ ! -d "$TRACEDIR/events/kprobes/vfs_read" ]; then
	echo "FAIL: kprobe event kprobes/vfs_read does not exist"
	exit 1
fi

ENABLE=$(cat "$TRACEDIR/events/kprobes/vfs_read/enable")
if [ "$ENABLE" != "1" ]; then
	echo "FAIL: kprobe event kprobes/vfs_read is not enabled ($ENABLE)"
	exit 1
fi

echo "PASS: 01-kprobe"
exit 0

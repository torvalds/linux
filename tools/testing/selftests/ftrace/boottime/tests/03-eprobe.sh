#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check eprobe event bootconfig settings on tracefs
TRACEDIR="/sys/kernel/tracing"

if [ -f /proc/bootconfig ] && grep -q "dump_bconf" /proc/cmdline 2>/dev/null; then
	echo "=== /proc/bootconfig ==="
	cat /proc/bootconfig
	echo "========================"
fi

if [ ! -d "$TRACEDIR/events/eprobes/ep_read" ]; then
	echo "FAIL: eprobe event eprobes/ep_read does not exist"
	exit 1
fi

ENABLE=$(cat "$TRACEDIR/events/eprobes/ep_read/enable")
if [ "$ENABLE" != "1" ]; then
	echo "FAIL: eprobe event eprobes/ep_read is not enabled ($ENABLE)"
	exit 1
fi

echo "PASS: 03-eprobe"
exit 0

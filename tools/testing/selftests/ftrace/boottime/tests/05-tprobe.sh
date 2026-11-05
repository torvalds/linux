#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check tracepoint probe event bootconfig settings on tracefs
TRACEDIR="/sys/kernel/tracing"

if [ -f /proc/bootconfig ] && grep -q "dump_bconf" /proc/cmdline 2>/dev/null; then
	echo "=== /proc/bootconfig ==="
	cat /proc/bootconfig
	echo "========================"
fi

if [ ! -d "$TRACEDIR/events/tracepoints/tp_sched" ]; then
	echo "FAIL: tracepoint probe event tracepoints/tp_sched does not exist"
	exit 1
fi

ENABLE=$(cat "$TRACEDIR/events/tracepoints/tp_sched/enable")
if [ "$ENABLE" != "1" ]; then
	echo "FAIL: tracepoint probe event tracepoints/tp_sched is not enabled ($ENABLE)"
	exit 1
fi

echo "PASS: 05-tprobe"
exit 0

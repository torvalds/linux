#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check instance bootconfig settings on tracefs
TRACEDIR="/sys/kernel/tracing"

if [ -f /proc/bootconfig ] && grep -q "dump_bconf" /proc/cmdline 2>/dev/null; then
	echo "=== /proc/bootconfig ==="
	cat /proc/bootconfig
	echo "========================"
fi

if [ ! -d "$TRACEDIR/instances/foo" ]; then
	echo "FAIL: trace instance foo does not exist"
	exit 1
fi

if [ ! -d "$TRACEDIR/instances/foo/events/sched/sched_switch" ]; then
	echo "FAIL: event sched_switch does not exist in instance foo"
	exit 1
fi

ENABLE=$(cat "$TRACEDIR/instances/foo/events/sched/sched_switch/enable")
if [ "$ENABLE" != "1" ]; then
	echo "FAIL: event sched_switch is not enabled in instance foo ($ENABLE)"
	exit 1
fi

echo "PASS: 06-instance"
exit 0

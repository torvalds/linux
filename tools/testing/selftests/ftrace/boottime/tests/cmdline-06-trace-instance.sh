#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check trace_instance= kernel command-line setting
TRACEDIR="/sys/kernel/tracing"

if [ ! -d "$TRACEDIR/instances/bar" ]; then
	echo "FAIL: trace instance bar does not exist"
	exit 1
fi

if [ ! -d "$TRACEDIR/instances/bar/events/sched/sched_switch" ]; then
	echo "FAIL: event sched_switch does not exist in instance bar"
	exit 1
fi

ENABLE=$(cat "$TRACEDIR/instances/bar/events/sched/sched_switch/enable")
if [ "$ENABLE" != "1" ]; then
	echo "FAIL: event sched_switch is not enabled in instance bar ($ENABLE)"
	exit 1
fi

echo "PASS: cmdline-06-trace-instance"
exit 0

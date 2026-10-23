#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check trace_event= kernel command-line setting
TRACEDIR="/sys/kernel/tracing"

if [ ! -d "$TRACEDIR/events/sched/sched_switch" ]; then
	echo "FAIL: event sched:sched_switch does not exist"
	exit 1
fi

if [ ! -d "$TRACEDIR/events/kmem/kmalloc" ]; then
	echo "FAIL: event kmem:kmalloc does not exist"
	exit 1
fi

ENABLE1=$(cat "$TRACEDIR/events/sched/sched_switch/enable")
ENABLE2=$(cat "$TRACEDIR/events/kmem/kmalloc/enable")

if [ "$ENABLE1" != "1" ] || [ "$ENABLE2" != "1" ]; then
	echo "FAIL: events not enabled (sched_switch=$ENABLE1, kmalloc=$ENABLE2)"
	exit 1
fi

echo "PASS: cmdline-02-trace-event"
exit 0

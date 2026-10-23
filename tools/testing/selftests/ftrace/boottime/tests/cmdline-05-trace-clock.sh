#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check trace_clock= kernel command-line setting
TRACEDIR="/sys/kernel/tracing"

if [ ! -f "$TRACEDIR/trace_clock" ]; then
	echo "FAIL: trace_clock file does not exist"
	exit 1
fi

if ! grep -q '\[global\]' "$TRACEDIR/trace_clock"; then
	CLOCK=$(cat "$TRACEDIR/trace_clock")
	echo "FAIL: trace_clock is not set to global ($CLOCK)"
	exit 1
fi

echo "PASS: cmdline-05-trace-clock"
exit 0

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check ftrace= kernel command-line tracer setting
TRACEDIR="/sys/kernel/tracing"

if [ ! -f "$TRACEDIR/current_tracer" ]; then
	echo "FAIL: current_tracer does not exist"
	exit 1
fi

read -r TRACER _ < "$TRACEDIR/current_tracer"
if [ "$TRACER" != "function" ]; then
	echo "FAIL: current_tracer is '$TRACER', expected 'function'"
	exit 1
fi

echo "PASS: cmdline-01-ftrace"
exit 0

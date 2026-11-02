#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check trace_options= kernel command-line setting
TRACEDIR="/sys/kernel/tracing"

if [ ! -f "$TRACEDIR/trace_options" ]; then
	echo "FAIL: trace_options file does not exist"
	exit 1
fi

if ! grep -qw "sym-addr" "$TRACEDIR/trace_options"; then
	echo "FAIL: sym-addr option is not set in trace_options"
	exit 1
fi

if ! grep -qw "verbose" "$TRACEDIR/trace_options"; then
	echo "FAIL: verbose option is not set in trace_options"
	exit 1
fi

echo "PASS: cmdline-04-trace-options"
exit 0

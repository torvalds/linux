#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# APPLETS: awk
# Check trace_buf_size= kernel command-line setting
TRACEDIR="/sys/kernel/tracing"

if [ ! -f "$TRACEDIR/buffer_size_kb" ]; then
	echo "FAIL: buffer_size_kb does not exist"
	exit 1
fi

BUF_RAW=$(cat "$TRACEDIR/buffer_size_kb")
case "$BUF_RAW" in
	*"expanded:"*)
		BUFSIZE=$(echo "$BUF_RAW" | sed -n 's/.*expanded: *\([0-9]*\).*/\1/p')
		;;
	*)
		BUFSIZE=$(echo "$BUF_RAW" | awk '{print $1}')
		;;
esac

if [ -z "$BUFSIZE" ] || [ "$BUFSIZE" -lt 2048 ]; then
	echo "FAIL: buffer_size_kb is '$BUF_RAW', expected >= 2048"
	exit 1
fi

echo "PASS: cmdline-03-trace-buf-size"
exit 0

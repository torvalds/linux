#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check synthetic event bootconfig settings on tracefs
TRACEDIR="/sys/kernel/tracing"

if [ -f /proc/bootconfig ] && grep -q "dump_bconf" /proc/cmdline 2>/dev/null; then
	echo "=== /proc/bootconfig ==="
	cat /proc/bootconfig
	echo "========================"
fi

if [ ! -d "$TRACEDIR/events/synthetic/boot_lat" ]; then
	echo "FAIL: synthetic event synthetic/boot_lat does not exist"
	exit 1
fi

ENABLE=$(cat "$TRACEDIR/events/synthetic/boot_lat/enable")
if [ "$ENABLE" != "1" ]; then
	echo "FAIL: synthetic event synthetic/boot_lat is not enabled ($ENABLE)"
	exit 1
fi

echo "PASS: 02-synth"
exit 0

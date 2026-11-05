#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check persistent ring buffer across guest crash/reboot
# REBOOT: 1
TRACEDIR="/sys/kernel/tracing"

if [ ! -f "$TRACEDIR/instances/boot_map/trace" ]; then
	echo "FAIL: persistent trace instance boot_map/trace file missing"
	exit 1
fi

# Check if Boot 1 marker was already written
if grep -q "BOOT1_MARKER" "$TRACEDIR/instances/boot_map/trace" 2>/dev/null; then
	# Second boot: verify persistent ring buffer content from first boot
	echo "PASS: persistent-01-reserve-mem"
	exit 0
fi

# First boot: write marker to persistent buffer and trigger kernel crash/reboot
echo "BOOT1_MARKER" > "$TRACEDIR/instances/boot_map/trace_marker"
sync

# Trigger reboot to restart into second boot
echo b > /proc/sysrq-trigger 2>/dev/null || echo c > /proc/sysrq-trigger 2>/dev/null || true
sleep 5
echo "FAIL: reboot trigger failed on first boot"
exit 1

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2026, Google LLC.
# Check persistent backup instance across guest crash/reboot
# REBOOT: 1
TRACEDIR="/sys/kernel/tracing"

# Ensure both boot_map/trace and backup/trace files exist
if [ ! -f "$TRACEDIR/instances/boot_map/trace" ]; then
	echo "FAIL: boot_map/trace file missing"
	exit 1
fi

if [ ! -f "$TRACEDIR/instances/backup/trace" ]; then
	echo "FAIL: backup/trace file missing"
	exit 1
fi

# Check if BOOT1_MARKER is in boot_map/trace (indicates second boot)
if grep -q "BOOT1_MARKER" "$TRACEDIR/instances/boot_map/trace" 2>/dev/null; then
	# Second boot: verify BOOT1_MARKER was copied into backup/trace from Boot 1
	if grep -q "BOOT1_MARKER" "$TRACEDIR/instances/backup/trace" 2>/dev/null; then
		echo "PASS: persistent-02-backup-instance"
		exit 0
	else
		echo "FAIL: BOOT1_MARKER found in boot_map/trace" \
			"but missing from backup/trace on second boot"
		exit 1
	fi
fi

# First boot: write BOOT1_MARKER to boot_map and reboot via sysrq-trigger
echo "BOOT1_MARKER" > "$TRACEDIR/instances/boot_map/trace_marker"
sync

# Trigger reboot to restart into second boot
echo b > /proc/sysrq-trigger 2>/dev/null || echo c > /proc/sysrq-trigger 2>/dev/null || true
sleep 5
echo "FAIL: reboot trigger failed on first boot"
exit 1

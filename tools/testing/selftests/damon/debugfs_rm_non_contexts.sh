#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test putting non-ctx files/dirs to rm_contexts file
# ===================================================

dmesg -C

for file in "$DBGFS/"*
do
	echo "$(basename "$f")" > "$DBGFS/rm_contexts"
	if dmesg | grep -q BUG
	then
		dmesg
		exit 1
	fi
done

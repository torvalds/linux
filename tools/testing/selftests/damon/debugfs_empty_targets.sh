#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test empty targets case
# =======================

orig_target_ids=$(cat "$DBGFS/target_ids")
echo "" > "$DBGFS/target_ids"

if [ -f "$DBGFS/monitor_on_DEPRECATED" ]
then
	monitor_on_file="$DBGFS/monitor_on_DEPRECATED"
else
	monitor_on_file="$DBGFS/monitor_on"
fi

orig_monitor_on=$(cat "$monitor_on_file")
test_write_fail "$monitor_on_file" "on" "orig_monitor_on" "empty target ids"
echo "$orig_target_ids" > "$DBGFS/target_ids"

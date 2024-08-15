#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test empty targets case
# =======================

orig_target_ids=$(cat "$DBGFS/target_ids")
echo "" > "$DBGFS/target_ids"
orig_monitor_on=$(cat "$DBGFS/monitor_on")
test_write_fail "$DBGFS/monitor_on" "on" "orig_monitor_on" "empty target ids"
echo "$orig_target_ids" > "$DBGFS/target_ids"

#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test target_ids file
# ====================

file="$DBGFS/target_ids"
orig_content=$(cat "$file")

test_write_succ "$file" "1 2 3 4" "$orig_content" "valid input"
test_write_succ "$file" "1 2 abc 4" "$orig_content" "still valid input"
test_content "$file" "$orig_content" "1 2" "non-integer was there"
test_write_succ "$file" "abc 2 3" "$orig_content" "the file allows wrong input"
test_content "$file" "$orig_content" "" "wrong input written"
test_write_succ "$file" "" "$orig_content" "empty input"
test_content "$file" "$orig_content" "" "empty input written"
echo "$orig_content" > "$file"

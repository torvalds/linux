#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test schemes file
# =================

file="$DBGFS/schemes"
orig_content=$(cat "$file")

test_write_succ "$file" "1 2 3 4 5 6 4 0 0 0 1 2 3 1 100 3 2 1" \
	"$orig_content" "valid input"
test_write_fail "$file" "1 2
3 4 5 6 3 0 0 0 1 2 3 1 100 3 2 1" "$orig_content" "multi lines"
test_write_succ "$file" "" "$orig_content" "disabling"
test_write_fail "$file" "2 1 2 1 10 1 3 10 1 1 1 1 1 1 1 1 2 3" \
	"$orig_content" "wrong condition ranges"
echo "$orig_content" > "$file"

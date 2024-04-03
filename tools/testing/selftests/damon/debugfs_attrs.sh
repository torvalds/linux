#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source _debugfs_common.sh

# Test attrs file
# ===============

file="$DBGFS/attrs"
orig_content=$(cat "$file")

test_write_succ "$file" "1 2 3 4 5" "$orig_content" "valid input"
test_write_fail "$file" "1 2 3 4" "$orig_content" "no enough fields"
test_write_fail "$file" "1 2 3 5 4" "$orig_content" \
	"min_nr_regions > max_nr_regions"
test_content "$file" "$orig_content" "1 2 3 4 5" "successfully written"
echo "$orig_content" > "$file"

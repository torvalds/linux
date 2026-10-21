#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Read tracefs values and print them.
# Usage: check-tracefs-value.sh <relative_path1> [<relative_path2> ...]
# Each path is relative to /sys/kernel/tracing/
# Output: one line per file in the format "path=value"

for file in "$@"; do
	read value < "/sys/kernel/tracing/$file"
	echo "$file=$value"
done

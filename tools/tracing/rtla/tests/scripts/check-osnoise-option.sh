#!/bin/bash
# SPDX-License-Identifier: GPL-2.0
# Check if osnoise options are enabled or disabled.
# Usage: check-osnoise-option.sh <OPTION1> [<OPTION2> ...]
# Output: one line per option in the format "OPTION=enabled" or "OPTION=disabled"

options=$(tr ' ' '\n' < /sys/kernel/tracing/osnoise/options)
for name in "$@"; do
	if echo "$options" | grep -q "^NO_${name}$"; then
		echo "$name=disabled"
	elif echo "$options" | grep -q "^${name}$"; then
		echo "$name=enabled"
	else
		echo "$name=unsupported"
	fi
done

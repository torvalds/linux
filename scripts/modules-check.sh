#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

# Check uniqueness of module names
check_same_name_modules()
{
	for m in $(sed 's:.*/::' modules.order modules.builtin | sort | uniq -d)
	do
		echo "warning: same basename if the following are built as modules:" >&2
		sed "/\/$m/!d;s:^kernel/:  :" modules.order modules.builtin >&2
	done
}

check_same_name_modules

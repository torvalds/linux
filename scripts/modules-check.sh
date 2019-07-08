#!/bin/sh
# SPDX-License-Identifier: GPL-2.0

set -e

# Check uniqueness of module names
check_same_name_modules()
{
	for m in $(sed 's:.*/::' modules.order | sort | uniq -d)
	do
		echo "warning: same module names found:" >&2
		sed -n "/\/$m/s:^kernel/:  :p" modules.order >&2
	done
}

check_same_name_modules

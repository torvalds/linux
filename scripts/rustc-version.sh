#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Usage: $ ./rustc-version.sh rustc
#
# Print the Rust compiler version in a 6 or 7-digit form.

# Convert the version string x.y.z to a canonical up-to-7-digits form.
#
# Note that this function uses one more digit (compared to other
# instances in other version scripts) to give a bit more space to
# `rustc` since it will reach 1.100.0 in late 2026.
get_canonical_version()
{
	IFS=.
	set -- $1
	echo $((100000 * $1 + 100 * $2 + $3))
}

if output=$("$@" --version 2>/dev/null); then
	set -- $output
	get_canonical_version $2
else
	echo 0
	exit 1
fi

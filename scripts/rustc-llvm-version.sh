#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Usage: $ ./rustc-llvm-version.sh rustc
#
# Print the LLVM version that the Rust compiler uses in a 6 digit form.

# Convert the version string x.y.z to a canonical up-to-6-digits form.
get_canonical_version()
{
	IFS=.
	set -- $1
	echo $((10000 * $1 + 100 * $2 + $3))
}

if output=$("$@" --version --verbose 2>/dev/null | grep LLVM); then
	set -- $output
	get_canonical_version $3
else
	echo 0
	exit 1
fi

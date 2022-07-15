#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Usage: $ ./scripts/lld-version.sh ld.lld
#
# Print the linker version of `ld.lld' in a 5 or 6-digit form
# such as `100001' for ld.lld 10.0.1 etc.

set -e

# Convert the version string x.y.z to a canonical 5 or 6-digit form.
get_canonical_version()
{
	IFS=.
	set -- $1

	# If the 2nd or 3rd field is missing, fill it with a zero.
	echo $((10000 * $1 + 100 * ${2:-0} + ${3:-0}))
}

# Get the first line of the --version output.
IFS='
'
set -- $(LC_ALL=C "$@" --version)

# Split the line on spaces.
IFS=' '
set -- $1

while [ $# -gt 1 -a "$1" != "LLD" ]; do
	shift
done
if [ "$1" = LLD ]; then
	echo $(get_canonical_version ${2%-*})
else
	echo 0
fi

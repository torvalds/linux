#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Print the linker name and its version in a 5 or 6-digit form.
# Also, perform the minimum version check.

set -e

# When you raise the minimum linker version, please update
# Documentation/process/changes.rst as well.
bfd_min_version=2.23.0
lld_min_version=10.0.1

# Convert the version string x.y.z to a canonical 5 or 6-digit form.
get_canonical_version()
{
	IFS=.
	set -- $1

	# If the 2nd or 3rd field is missing, fill it with a zero.
	#
	# The 4th field, if present, is ignored.
	# This occurs in development snapshots as in 2.35.1.20201116
	echo $((10000 * $1 + 100 * ${2:-0} + ${3:-0}))
}

orig_args="$@"

# Get the first line of the --version output.
IFS='
'
set -- $("$@" --version)

# Split the line on spaces.
IFS=' '
set -- $1

if [ "$1" = GNU -a "$2" = ld ]; then
	shift $(($# - 1))
	version=$1
	min_version=$bfd_min_version
	name=BFD
	disp_name="GNU ld"
elif [ "$1" = GNU -a "$2" = gold ]; then
	echo "gold linker is not supported as it is not capable of linking the kernel proper." >&2
	exit 1
else
	while [ $# -gt 1 -a "$1" != "LLD" ]; do
		shift
	done

	if [ "$1" = LLD ]; then
		version=$2
		min_version=$lld_min_version
		name=LLD
		disp_name=LLD
	else
		echo "$orig_args: unknown linker" >&2
		exit 1
	fi
fi

# Some distributions append a package release number, as in 2.34-4.fc32
# Trim the hyphen and any characters that follow.
version=${version%-*}

cversion=$(get_canonical_version $version)
min_cversion=$(get_canonical_version $min_version)

if [ "$cversion" -lt "$min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Linker is too old."
	echo >&2 "***   Your $disp_name version:    $version"
	echo >&2 "***   Minimum $disp_name version: $min_version"
	echo >&2 "***"
	exit 1
fi

echo $name $cversion

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Print the assembler name and its version in a 5 or 6-digit form.
# Also, perform the minimum version check.
# (If it is the integrated assembler, return 0 as the version, and
# skip the version check.)

set -e

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

# Clang fails to handle -Wa,--version unless -fno-integrated-as is given.
# We check -fintegrated-as, expecting it is explicitly passed in for the
# integrated assembler case.
check_integrated_as()
{
	while [ $# -gt 0 ]; do
		if [ "$1" = -fintegrated-as ]; then
			# For the integrated assembler, we do not check the
			# version here. It is the same as the clang version, and
			# it has been already checked by scripts/cc-version.sh.
			echo LLVM 0
			exit 0
		fi
		shift
	done
}

check_integrated_as "$@"

orig_args="$@"

# Get the first line of the --version output.
IFS='
'
set -- $(LC_ALL=C "$@" -Wa,--version -c -x assembler /dev/null -o /dev/null 2>/dev/null)

# Split the line on spaces.
IFS=' '
set -- $1

min_tool_version=$(dirname $0)/min-tool-version.sh

if [ "$1" = GNU -a "$2" = assembler ]; then
	shift $(($# - 1))
	version=$1
	min_version=$($min_tool_version binutils)
	name=GNU
else
	echo "$orig_args: unknown assembler invoked" >&2
	exit 1
fi

# Some distributions append a package release number, as in 2.34-4.fc32
# Trim the hyphen and any characters that follow.
version=${version%-*}

cversion=$(get_canonical_version $version)
min_cversion=$(get_canonical_version $min_version)

if [ "$cversion" -lt "$min_cversion" ]; then
	echo >&2 "***"
	echo >&2 "*** Assembler is too old."
	echo >&2 "***   Your $name assembler version:    $version"
	echo >&2 "***   Minimum $name assembler version: $min_version"
	echo >&2 "***"
	exit 1
fi

echo $name $cversion

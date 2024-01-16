#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only

# Dummy script that always succeeds.

# Check if the first parameter appears in the rest. Succeeds if found.
# This helper is useful if a particular option was passed to this script.
# Typically used like this:
#   arg_contain <word-you-are-searching-for> "$@"
arg_contain ()
{
	search="$1"
	shift

	while [ $# -gt 0 ]
	do
		if [ "$search" = "$1" ]; then
			return 0
		fi
		shift
	done

	return 1
}

if arg_contain --version "$@" || arg_contain -v "$@"; then
	progname=$(basename $0)
	echo "GNU $progname (scripts/dummy-tools/$progname) 2.50"
	exit 0
fi

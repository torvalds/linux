#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Generate a syscall table header.
#
# Each line of the syscall table should have the following format:
#
# NR ABI NAME [NATIVE] [COMPAT]
#
# NR       syscall number
# ABI      ABI name
# NAME     syscall name
# NATIVE   native entry point (optional)
# COMPAT   compat entry point (optional)

set -e

usage() {
	echo >&2 "usage: $0 [--abis ABIS] INFILE OUTFILE" >&2
	echo >&2
	echo >&2 "  INFILE    input syscall table"
	echo >&2 "  OUTFILE   output header file"
	echo >&2
	echo >&2 "options:"
	echo >&2 "  --abis ABIS        ABI(s) to handle (By default, all lines are handled)"
	exit 1
}

# default unless specified by options
abis=

while [ $# -gt 0 ]
do
	case $1 in
	--abis)
		abis=$(echo "($2)" | tr ',' '|')
		shift 2;;
	-*)
		echo "$1: unknown option" >&2
		usage;;
	*)
		break;;
	esac
done

if [ $# -ne 2 ]; then
	usage
fi

infile="$1"
outfile="$2"

nxt=0

grep -E "^[0-9]+[[:space:]]+$abis" "$infile" | {

	while read nr abi name native compat ; do

		if [ $nxt -gt $nr ]; then
			echo "error: $infile: syscall table is not sorted or duplicates the same syscall number" >&2
			exit 1
		fi

		while [ $nxt -lt $nr ]; do
			echo "__SYSCALL($nxt, sys_ni_syscall)"
			nxt=$((nxt + 1))
		done

		if [ -n "$compat" ]; then
			echo "__SYSCALL_WITH_COMPAT($nr, $native, $compat)"
		elif [ -n "$native" ]; then
			echo "__SYSCALL($nr, $native)"
		else
			echo "__SYSCALL($nr, sys_ni_syscall)"
		fi
		nxt=$((nr + 1))
	done
} > "$outfile"

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# Generate a syscall number header.
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
	echo >&2 "usage: $0 [--abis ABIS] [--emit-nr] [--offset OFFSET] [--prefix PREFIX] INFILE OUTFILE" >&2
	echo >&2
	echo >&2 "  INFILE    input syscall table"
	echo >&2 "  OUTFILE   output header file"
	echo >&2
	echo >&2 "options:"
	echo >&2 "  --abis ABIS        ABI(s) to handle (By default, all lines are handled)"
	echo >&2 "  --emit-nr          Emit the macro of the number of syscalls (__NR_syscalls)"
	echo >&2 "  --offset OFFSET    The offset of syscall numbers"
	echo >&2 "  --prefix PREFIX    The prefix to the macro like __NR_<PREFIX><NAME>"
	exit 1
}

# default unless specified by options
abis=
emit_nr=
offset=
prefix=

while [ $# -gt 0 ]
do
	case $1 in
	--abis)
		abis=$(echo "($2)" | tr ',' '|')
		shift 2;;
	--emit-nr)
		emit_nr=1
		shift 1;;
	--offset)
		offset=$2
		shift 2;;
	--prefix)
		prefix=$2
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

guard=_UAPI_ASM_$(basename "$outfile" |
	sed -e 'y/abcdefghijklmnopqrstuvwxyz/ABCDEFGHIJKLMNOPQRSTUVWXYZ/' \
	-e 's/[^A-Z0-9_]/_/g' -e 's/__/_/g')

grep -E "^[0-9A-Fa-fXx]+[[:space:]]+$abis" "$infile" | sort -n | {
	echo "#ifndef $guard"
	echo "#define $guard"
	echo

	max=0
	while read nr abi name native compat ; do

		max=$nr

		if [ -n "$offset" ]; then
			nr="($offset + $nr)"
		fi

		echo "#define __NR_$prefix$name $nr"
	done

	if [ -n "$emit_nr" ]; then
		echo
		echo "#ifdef __KERNEL__"
		echo "#define __NR_${prefix}syscalls $(($max + 1))"
		echo "#endif"
	fi

	echo
	echo "#endif /* $guard */"
} > "$outfile"

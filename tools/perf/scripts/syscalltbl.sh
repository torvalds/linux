#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
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

syscall_macro() {
    nr="$1"
    name="$2"

    echo "	[$nr] = \"$name\","
}

emit() {
    nr="$1"
    entry="$2"

    syscall_macro "$nr" "$entry"
}

echo "static const char *const syscalltbl[] = {" > $outfile

sorted_table=$(mktemp /tmp/syscalltbl.XXXXXX)
grep -E "^[0-9]+[[:space:]]+$abis" "$infile" | sort -n > $sorted_table

max_nr=0
# the params are: nr abi name entry compat
# use _ for intentionally unused variables according to SC2034
while read nr _ name _ _; do
    emit "$nr" "$name" >> $outfile
    max_nr=$nr
done < $sorted_table

rm -f $sorted_table

echo "};" >> $outfile

echo "#define SYSCALLTBL_MAX_ID ${max_nr}" >> $outfile

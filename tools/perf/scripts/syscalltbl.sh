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

sorted_table=$(mktemp /tmp/syscalltbl.XXXXXX)
grep -E "^[0-9]+[[:space:]]+$abis" "$infile" | sort -n > $sorted_table

echo "static const char *const syscall_num_to_name[] = {" > $outfile
# the params are: nr abi name entry compat
# use _ for intentionally unused variables according to SC2034
while read nr _ name _ _; do
	echo "	[$nr] = \"$name\"," >> $outfile
done < $sorted_table
echo "};" >> $outfile

echo "static const uint16_t syscall_sorted_names[] = {" >> $outfile

# When sorting by name, add a suffix of 0s upto 20 characters so that system
# calls that differ with a numerical suffix don't sort before those
# without. This default behavior of sort differs from that of strcmp used at
# runtime. Use sed to strip the trailing 0s suffix afterwards.
grep -E "^[0-9]+[[:space:]]+$abis" "$infile" | awk '{printf $3; for (i = length($3); i < 20; i++) { printf "0"; }; print " " $1}'| sort | sed 's/\([a-zA-Z1-9]\+\)0\+ \([0-9]\+\)/\1 \2/' > $sorted_table
while read name nr; do
	echo "	$nr,	/* $name */" >> $outfile
done < $sorted_table
echo "};" >> $outfile

rm -f $sorted_table

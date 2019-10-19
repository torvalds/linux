#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate system call table for perf. Derived from
# powerpc script.
#
# Copyright IBM Corp. 2017
# Author(s):  Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
# Changed by: Ravi Bangoria <ravi.bangoria@linux.vnet.ibm.com>
# Changed by: Kim Phillips <kim.phillips@arm.com>

gcc=$1
hostcc=$2
incpath=$3
input=$4

if ! test -r $input; then
	echo "Could not read input file" >&2
	exit 1
fi

create_table_from_c()
{
	local sc nr last_sc

	create_table_exe=`mktemp ${TMPDIR:-/tmp}/create-table-XXXXXX`

	{

	cat <<-_EoHEADER
		#include <stdio.h>
		#include "$input"
		int main(int argc, char *argv[])
		{
	_EoHEADER

	while read sc nr; do
		printf "%s\n" "	printf(\"\\t[%d] = \\\"$sc\\\",\\n\", __NR_$sc);"
		last_sc=$sc
	done

	printf "%s\n" "	printf(\"#define SYSCALLTBL_ARM64_MAX_ID %d\\n\", __NR_$last_sc);"
	printf "}\n"

	} | $hostcc -I $incpath/include/uapi -o $create_table_exe -x c -

	$create_table_exe

	rm -f $create_table_exe
}

create_table()
{
	echo "static const char *syscalltbl_arm64[] = {"
	create_table_from_c
	echo "};"
}

$gcc -E -dM -x c -I $incpath/include/uapi $input \
	|sed -ne 's/^#define __NR_//p' \
	|sort -t' ' -k2 -nu	       \
	|create_table

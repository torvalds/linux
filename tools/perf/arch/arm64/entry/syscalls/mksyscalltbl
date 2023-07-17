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

create_sc_table()
{
	local sc nr max_nr

	while read sc nr; do
		printf "%s\n" "	[$nr] = \"$sc\","
		max_nr=$nr
	done

	echo "#define SYSCALLTBL_ARM64_MAX_ID $max_nr"
}

create_table()
{
	echo "#include \"$input\""
	echo "static const char *const syscalltbl_arm64[] = {"
	create_sc_table
	echo "};"
}

$gcc -E -dM -x c -I $incpath/include/uapi $input \
	|awk '$2 ~ "__NR" && $3 !~ "__NR3264_" {
		sub("^#define __NR(3264)?_", "");
		print | "sort -k2 -n"}' \
	|create_table

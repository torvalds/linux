#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Generate system call table for perf. Derived from
# powerpc script.
#
# Author(s):  Ming Wang <wangming01@loongson.cn>
# Author(s):  Huacai Chen <chenhuacai@loongson.cn>
# Copyright (C) 2020-2023 Loongson Technology Corporation Limited

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

	echo "#define SYSCALLTBL_LOONGARCH_MAX_ID $max_nr"
}

create_table()
{
	echo "#include \"$input\""
	echo "static const char *const syscalltbl_loongarch[] = {"
	create_sc_table
	echo "};"
}

$gcc -E -dM -x c -I $incpath/include/uapi $input \
	|awk '$2 ~ "__NR" && $3 !~ "__NR3264_" {
		sub("^#define __NR(3264)?_", "");
		print | "sort -k2 -n"}' \
	|create_table

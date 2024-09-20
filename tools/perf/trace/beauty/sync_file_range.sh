#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -ne 1 ] ; then
	linux_header_dir=tools/perf/trace/beauty/include/uapi/linux/
else
	linux_header_dir=$1
fi

linux_fs=${linux_header_dir}/fs.h

printf "static const char *sync_file_range_flags[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+SYNC_FILE_RANGE_([[:alnum:]_]+)[[:space:]]+([[:xdigit:]]+)[[:space:]]*.*'
grep -E $regex ${linux_fs} | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[ilog2(%s) + 1] = \"%s\",\n"
printf "};\n"

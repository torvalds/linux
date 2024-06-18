#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && beauty_uapi_linux_dir=$1 || beauty_uapi_linux_dir=tools/perf/trace/beauty/include/uapi/linux/

printf "static const char *prctl_options[] = {\n"
regex='^#define[[:space:]]{1}PR_(\w+)[[:space:]]*([[:xdigit:]]+)([[:space:]]*/.*)?$'
grep -E $regex ${beauty_uapi_linux_dir}/prctl.h | grep -v PR_SET_PTRACER | \
	sed -E "s%$regex%\2 \1%g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

printf "static const char *prctl_set_mm_options[] = {\n"
regex='^#[[:space:]]+define[[:space:]]+PR_SET_MM_(\w+)[[:space:]]*([[:digit:]]+).*'
grep -E $regex ${beauty_uapi_linux_dir}/prctl.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && beauty_uapi_linux_dir=$1 || beauty_uapi_linux_dir=tools/perf/trace/beauty/include/uapi/linux/

printf "static const char *mount_flags[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+MS_([[:alnum:]_]+)[[:space:]]+([[:digit:]]+)[[:space:]]*.*'
grep -E $regex ${beauty_uapi_linux_dir}/mount.h | grep -E -v '(MSK|VERBOSE|MGC_VAL)\>' | \
	sed -r "s/$regex/\2 \2 \1/g" | sort -n | \
	xargs printf "\t[%s ? (ilog2(%s) + 1) : 0] = \"%s\",\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+MS_([[:alnum:]_]+)[[:space:]]+\(1<<([[:digit:]]+)\)[[:space:]]*.*'
grep -E $regex ${beauty_uapi_linux_dir}/mount.h | \
	sed -r "s/$regex/\2 \1/g" | \
	xargs printf "\t[%s + 1] = \"%s\",\n"
printf "};\n"

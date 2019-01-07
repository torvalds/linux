#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

printf "static const char *mount_flags[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+MS_([[:alnum:]_]+)[[:space:]]+([[:digit:]]+)[[:space:]]*.*'
egrep $regex ${header_dir}/fs.h | egrep -v '(MSK|VERBOSE|MGC_VAL)\>' | \
	sed -r "s/$regex/\2 \2 \1/g" | sort -n | \
	xargs printf "\t[%s ? (ilog2(%s) + 1) : 0] = \"%s\",\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+MS_([[:alnum:]_]+)[[:space:]]+\(1<<([[:digit:]]+)\)[[:space:]]*.*'
egrep $regex ${header_dir}/fs.h | \
	sed -r "s/$regex/\2 \1/g" | \
	xargs printf "\t[%s + 1] = \"%s\",\n"
printf "};\n"

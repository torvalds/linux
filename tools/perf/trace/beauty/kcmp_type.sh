#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

printf "static const char *kcmp_types[] = {\n"
regex='^[[:space:]]+(KCMP_(\w+)),'
grep -E $regex ${header_dir}/kcmp.h | grep -v KCMP_TYPES, | \
	sed -r "s/$regex/\1 \2/g" | \
	xargs printf "\t[%s]\t= \"%s\",\n"
printf "};\n"

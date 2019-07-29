#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/asm-generic/

printf "static const char *pkey_alloc_access_rights[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+PKEY_([[:alnum:]_]+)[[:space:]]+(0x[[:xdigit:]]+)[[:space:]]*'
egrep $regex ${header_dir}/mman-common.h	| \
	sed -r "s/$regex/\2 \2 \1/g"	| \
	sort | xargs printf "\t[%s ? (ilog2(%s) + 1) : 0] = \"%s\",\n"
printf "};\n"

#!/bin/sh
# Copyright (C) 2018, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

fs_header=${header_dir}/fs.h

printf "static const char *rename_flags[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+RENAME_([[:alnum:]_]+)[[:space:]]+\(1[[:space:]]*<<[[:space:]]*([[:xdigit:]]+)[[:space:]]*\)[[:space:]]*.*'
grep -E -q $regex ${fs_header} && \
(grep -E $regex ${fs_header} | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[%d + 1] = \"%s\",\n")
printf "};\n"

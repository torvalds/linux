#!/bin/sh -eu
# SPDX-License-Identifier: GPL-2.0

[ ! -x "$(command -v "$1")" ] && exit 1

tmp_file=$(mktemp)
trap "rm -f $tmp_file" EXIT

cat << EOF >$tmp_file
static inline int u(const int *q)
{
	__typeof_unqual__(*q) v = *q;
	return v;
}
EOF

# sparse happily exits with 0 on error so validate
# there is none on stderr. Use awk as grep is a pain with sh -e
$@ $tmp_file 2>&1 | awk -v c=1 '/error/{c=0}END{print c}'

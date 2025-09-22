#!/bin/sh
# $OpenBSD: genmap.sh,v 1.7 2015/01/16 06:39:58 deraadt Exp $

# Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

TOK=$(echo ${2} | tr "[:lower:]" "[:upper:]")
tok=$(echo ${2} | tr "[:upper:]" "[:lower:]")

MAP=$(grep "struct iked_constmap" $1 |
	sed -Ee "s/.*${tok}_([^_]+)_map.*/\1/g")

# Print license/copyright notice and headers
cat <<EOF
/* Automatically generated from $1, do not edit */
EOF
sed -n '1,/^ \*\//p' $1
cat <<EOF

#include <sys/types.h>

#include "types.h"
#include "${tok}.h"

EOF

for i in $MAP; do
	lower=$(echo $i | tr "[:upper:]" "[:lower:]")
	upper=$(echo $i | tr "[:lower:]" "[:upper:]")

	echo "struct iked_constmap ${tok}_${lower}_map[] = {"

	X="${TOK}_${upper}"
	grep "$X" $1 | grep -v '\\' | sed -Ee \
	    "s/#define.*${X}_([^[:blank:]]+).*\/\* (.+) \*\/$\
/	{ ${X}_\1, \"\1\", \"\2\" },/"

	echo "	{ 0 }"
	echo "};"
done

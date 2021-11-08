#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -gt 0 ] ; then
	uapi_header_dir=$1
else
	uapi_header_dir=tools/include/uapi/linux/
fi

printf "static const char *socket_ipproto[] = {\n"
regex='^[[:space:]]+IPPROTO_(\w+)[[:space:]]+=[[:space:]]+([[:digit:]]+),.*'

egrep $regex ${uapi_header_dir}/in.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

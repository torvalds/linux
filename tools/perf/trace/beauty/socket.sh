#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -gt 0 ] ; then
	uapi_header_dir=$1
else
	uapi_header_dir=tools/include/uapi/linux/
fi

printf "static const char *socket_ipproto[] = {\n"
ipproto_regex='^[[:space:]]+IPPROTO_(\w+)[[:space:]]+=[[:space:]]+([[:digit:]]+),.*'

egrep $ipproto_regex ${uapi_header_dir}/in.h | \
	sed -r "s/$ipproto_regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

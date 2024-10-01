#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -gt 0 ] ; then
	uapi_header_dir=$1
	beauty_header_dir=$2
else
	uapi_header_dir=tools/include/uapi/linux/
	beauty_header_dir=tools/perf/trace/beauty/include/linux/
fi

printf "static const char *socket_ipproto[] = {\n"
ipproto_regex='^[[:space:]]+IPPROTO_(\w+)[[:space:]]+=[[:space:]]+([[:digit:]]+),.*'

egrep $ipproto_regex ${uapi_header_dir}/in.h | \
	sed -r "s/$ipproto_regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n\n"

printf "static const char *socket_level[] = {\n"
socket_level_regex='^#define[[:space:]]+SOL_(\w+)[[:space:]]+([[:digit:]]+)([[:space:]]+\/.*)?'

egrep $socket_level_regex ${beauty_header_dir}/socket.h | \
	sed -r "s/$socket_level_regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n\n"

printf 'DEFINE_STRARRAY(socket_level, "SOL_");\n'

#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

# This one uses a copy from the kernel sources headers that is in a
# place used just for these tools/perf/beauty/ usage, we shouldn't not
# put it in tools/include/linux otherwise they would be used in the
# normal compiler building process and would drag needless stuff from the
# kernel.

# When what these scripts need is already in tools/include/ then use it,
# otherwise grab and check the copy from the kernel sources just for these
# string table building scripts.

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/perf/trace/beauty/include/linux/

printf "static const char *socket_families[] = {\n"
# #define AF_LOCAL	1	/* POSIX name for AF_UNIX	*/
regex='^#define[[:space:]]+AF_(\w+)[[:space:]]+([[:digit:]]+).*'

egrep $regex ${header_dir}/socket.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[%s] = \"%s\",\n" | \
	egrep -v "\"(UNIX|MAX)\""
printf "};\n"

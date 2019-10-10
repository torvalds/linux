#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -ne 1 ] ; then
	linux_header_dir=tools/include/uapi/linux
else
	linux_header_dir=$1
fi

linux_mount=${linux_header_dir}/mount.h

printf "static const char *fsconfig_cmds[] = {\n"
regex='^[[:space:]]*+FSCONFIG_([[:alnum:]_]+)[[:space:]]*=[[:space:]]*([[:digit:]]+)[[:space:]]*,[[:space:]]*.*'
egrep $regex ${linux_mount} | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

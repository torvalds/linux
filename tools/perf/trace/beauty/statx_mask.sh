#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -ne 1 ] ; then
	beauty_uapi_linux_dir=tools/perf/trace/beauty/include/uapi/linux/
else
	beauty_uapi_linux_dir=$1
fi

linux_stat=${beauty_uapi_linux_dir}/stat.h

printf "static const char *statx_mask[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+STATX_([^_]+[[:alnum:]_]+)[[:space:]]+(0x[[:xdigit:]]+)[[:space:]]*.*'
# STATX_BASIC_STATS its a bitmask formed by the mask in the normal stat struct
# STATX_ALL is another bitmask and deprecated
# STATX_ATTR_*: Attributes to be found in stx_attributes and masked in stx_attributes_mask
grep -E $regex ${linux_stat} | \
	grep -v STATX_ALL | \
	grep -v STATX_BASIC_STATS | \
	grep -v '\<STATX_ATTR_' | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[ilog2(%s) + 1] = \"%s\",\n"
printf "};\n"

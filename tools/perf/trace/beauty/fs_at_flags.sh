#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -ne 1 ] ; then
	beauty_uapi_linux_dir=tools/perf/trace/beauty/include/uapi/linux/
else
	beauty_uapi_linux_dir=$1
fi

linux_fcntl=${beauty_uapi_linux_dir}/fcntl.h

printf "static const char *fs_at_flags[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+AT_([^_]+[[:alnum:]_]+)[[:space:]]+(0x[[:xdigit:]]+)[[:space:]]*.*'
# AT_EACCESS is only meaningful to faccessat, so we will special case it there...
# AT_STATX_SYNC_TYPE is not a bit, its a mask of AT_STATX_SYNC_AS_STAT, AT_STATX_FORCE_SYNC and AT_STATX_DONT_SYNC
# AT_HANDLE_FID, AT_HANDLE_MNT_ID_UNIQUE and AT_HANDLE_CONNECTABLE are reusing values and are valid only for name_to_handle_at()
# AT_RENAME_NOREPLACE reuses 0x1 and is valid only for renameat2()
grep -E $regex ${linux_fcntl} | \
	grep -v AT_EACCESS | \
	grep -v AT_STATX_SYNC_TYPE | \
	grep -v AT_HANDLE_FID | \
	grep -v AT_HANDLE_MNT_ID_UNIQUE | \
	grep -v AT_HANDLE_CONNECTABLE | \
	grep -v AT_RENAME_NOREPLACE | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[ilog2(%s) + 1] = \"%s\",\n"
printf "};\n"

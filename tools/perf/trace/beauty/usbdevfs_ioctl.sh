#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && beauty_uapi_linux_dir=$1 || beauty_uapi_linux_dir=tools/perf/trace/beauty/include/uapi/linux/

# also as:
# #define USBDEVFS_CONNINFO_EX(len)  _IOC(_IOC_READ, 'U', 32, len)

printf "static const char *usbdevfs_ioctl_cmds[] = {\n"
regex="^#[[:space:]]*define[[:space:]]+USBDEVFS_(\w+)(\(\w+\))?[[:space:]]+_IO[CWR]{0,2}\([[:space:]]*(_IOC_\w+,[[:space:]]*)?'U'[[:space:]]*,[[:space:]]*([[:digit:]]+).*"
grep -E "$regex" ${beauty_uapi_linux_dir}/usbdevice_fs.h | grep -E -v 'USBDEVFS_\w+32[[:space:]]' | \
	sed -r "s/$regex/\4 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n\n"
printf "#if 0\n"
printf "static const char *usbdevfs_ioctl_32_cmds[] = {\n"
regex="^#[[:space:]]*define[[:space:]]+USBDEVFS_(\w+)[[:space:]]+_IO[WR]{0,2}\([[:space:]]*'U'[[:space:]]*,[[:space:]]*([[:digit:]]+).*"
grep -E $regex ${beauty_uapi_linux_dir}/usbdevice_fs.h | grep -E 'USBDEVFS_\w+32[[:space:]]' | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"
printf "#endif\n"

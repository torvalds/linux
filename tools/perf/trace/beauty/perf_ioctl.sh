#!/bin/sh

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

printf "static const char *perf_ioctl_cmds[] = {\n"
regex='^#[[:space:]]*define[[:space:]]+PERF_EVENT_IOC_(\w+)[[:space:]]+_IO[RW]*[[:space:]]*\([[:space:]]*.\$.[[:space:]]*,[[:space:]]*([[:digit:]]+).*'
egrep $regex ${header_dir}/perf_event.h	| \
	sed -r "s/$regex/\2 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

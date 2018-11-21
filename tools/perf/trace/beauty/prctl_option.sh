#!/bin/sh

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

printf "static const char *prctl_options[] = {\n"
regex='^#define[[:space:]]+PR_([GS]ET\w+)[[:space:]]*([[:xdigit:]]+).*'
egrep $regex ${header_dir}/prctl.h | grep -v PR_SET_PTRACER | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

printf "static const char *prctl_set_mm_options[] = {\n"
regex='^#[[:space:]]+define[[:space:]]+PR_SET_MM_(\w+)[[:space:]]*([[:digit:]]+).*'
egrep $regex ${header_dir}/prctl.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

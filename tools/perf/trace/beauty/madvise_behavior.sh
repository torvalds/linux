#!/bin/sh

header_dir=$1

printf "static const char *madvise_advices[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+MADV_([[:alnum:]_]+)[[:space:]]+([[:digit:]]+)[[:space:]]*.*'
egrep $regex ${header_dir}/mman-common.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort -n | xargs printf "\t[%s] = \"%s\",\n"
printf "};\n"

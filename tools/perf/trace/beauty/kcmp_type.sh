#!/bin/sh

header_dir=$1

printf "static const char *kcmp_types[] = {\n"
regex='^[[:space:]]+(KCMP_(\w+)),'
egrep $regex ${header_dir}/kcmp.h | grep -v KCMP_TYPES, | \
	sed -r "s/$regex/\1 \2/g" | \
	xargs printf "\t[%s]\t= \"%s\",\n"
printf "};\n"

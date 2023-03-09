#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

[ $# -eq 1 ] && header_dir=$1 || header_dir=tools/include/uapi/linux/

printf "static const char *fadvise_advices[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+POSIX_FADV_(\w+)[[:space:]]+([[:digit:]]+)[[:space:]]+.*'

grep -E $regex ${header_dir}/fadvise.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	sort | xargs printf "\t[%s] = \"%s\",\n" | \
	grep -v "[6].*DONTNEED" | grep -v "[7].*NOREUSE"
printf "};\n"

# XXX Fix this properly:

# The grep 6/7 DONTNEED/NOREUSE are a hack to filter out the s/390 oddity See
# tools/include/uapi/linux/fadvise.h for details.

# Probably fix this when generating the string tables per arch so that We can
# reliably process on arch FOO a perf.data file collected by 'perf trace
# record' on arch BAR, e.g. collect on s/390 and process on x86.

#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1

if [ $# -ne 2 ] ; then
	[ $# -eq 1 ] && hostarch=$1 || hostarch=`uname -m | sed -e s/i.86/x86/ -e s/x86_64/x86/`
	header_dir=tools/include/uapi/asm-generic
	arch_header_dir=tools/arch/${hostarch}/include/uapi/asm
else
	header_dir=$1
	arch_header_dir=$2
fi

arch_mman=${arch_header_dir}/mman.h

# those in egrep -vw are flags, we want just the bits

printf "static const char *mmap_flags[] = {\n"
regex='^[[:space:]]*#[[:space:]]*define[[:space:]]+MAP_([[:alnum:]_]+)[[:space:]]+(0x[[:xdigit:]]+)[[:space:]]*.*'
egrep -q $regex ${arch_mman} && \
(egrep $regex ${arch_mman} | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[ilog2(%s) + 1] = \"%s\",\n")
egrep -q '#[[:space:]]*include[[:space:]]+<uapi/asm-generic/mman.*' ${arch_mman} &&
(egrep $regex ${header_dir}/mman-common.h | \
	egrep -vw 'MAP_(UNINITIALIZED|TYPE|SHARED_VALIDATE)' | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[ilog2(%s) + 1] = \"%s\",\n")
egrep -q '#[[:space:]]*include[[:space:]]+<uapi/asm-generic/mman.h>.*' ${arch_mman} &&
(egrep $regex ${header_dir}/mman.h | \
	sed -r "s/$regex/\2 \1/g"	| \
	xargs printf "\t[ilog2(%s) + 1] = \"%s\",\n")
printf "};\n"

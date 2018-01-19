#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Run headers_$1 command for all suitable architectures

# Stop on error
set -e

do_command()
{
	if [ -f ${srctree}/arch/$2/include/asm/Kbuild ]; then
		make ARCH=$2 KBUILD_HEADERS=$1 headers_$1
	else
		printf "Ignoring arch: %s\n" ${arch}
	fi
}

archs=${HDR_ARCH_LIST:-$(ls ${srctree}/arch)}

for arch in ${archs}; do
	case ${arch} in
	um)        # no userspace export
		;;
	*)
		if [ -d ${srctree}/arch/${arch} ]; then
			do_command $1 ${arch}
		fi
		;;
	esac
done

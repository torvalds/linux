#!/bin/sh
# SPDX-License-Identifier: 0BSD
#
# This is a wrapper for xz to compress the kernel image using appropriate
# compression options depending on the architecture.
#
# Author: Lasse Collin <lasse.collin@tukaani.org>

BCJ=
LZMA2OPTS=

case $SRCARCH in
	x86)            BCJ=--x86 ;;
	powerpc)        BCJ=--powerpc ;;
	arm)            BCJ=--arm ;;
	sparc)          BCJ=--sparc ;;
esac

exec $XZ --check=crc32 $BCJ --lzma2=$LZMA2OPTS,dict=32MiB

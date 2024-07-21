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

# Use single-threaded mode because it compresses a little better
# (and uses less RAM) than multithreaded mode.
#
# For the best compression, the dictionary size shouldn't be
# smaller than the uncompressed kernel. 128 MiB dictionary
# needs less than 1400 MiB of RAM in single-threaded mode.
#
# On the archs that use this script to compress the kernel,
# decompression in the preboot code is done in single-call mode.
# Thus the dictionary size doesn't affect the memory requirements
# of the preboot decompressor at all.
exec $XZ --check=crc32 --threads=1 $BCJ --lzma2=$LZMA2OPTS,dict=128MiB

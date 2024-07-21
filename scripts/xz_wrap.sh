#!/bin/sh
# SPDX-License-Identifier: 0BSD
#
# This is a wrapper for xz to compress the kernel image using appropriate
# compression options depending on the architecture.
#
# Author: Lasse Collin <lasse.collin@tukaani.org>

# This has specialized settings for the following archs. However,
# XZ-compressed kernel isn't currently supported on every listed arch.
#
#   Arch        Align   Notes
#   arm          2/4    ARM and ARM-Thumb2
#   arm64         4
#   csky          2
#   loongarch     4
#   mips         2/4    MicroMIPS is 2-byte aligned
#   parisc        4
#   powerpc       4     Uses its own wrapper for compressors instead of this.
#   riscv        2/4
#   s390          2
#   sh            2
#   sparc         4
#   x86           1

# A few archs use 2-byte or 4-byte aligned instructions depending on
# the kernel config. This function is used to check if the relevant
# config option is set to "y".
is_enabled()
{
	grep -q "^$1=y$" include/config/auto.conf
}

# XZ_VERSION is needed to disable features that aren't available in
# old XZ Utils versions.
XZ_VERSION=$($XZ --robot --version) || exit
XZ_VERSION=$(printf '%s\n' "$XZ_VERSION" | sed -n 's/^XZ_VERSION=//p')

# Assume that no BCJ filter is available.
BCJ=

# Set the instruction alignment to 1, 2, or 4 bytes.
#
# Set the BCJ filter if one is available.
# It must match the #ifdef usage in lib/decompress_unxz.c.
case $SRCARCH in
	arm)
		if is_enabled CONFIG_THUMB2_KERNEL; then
			ALIGN=2
			BCJ=--armthumb
		else
			ALIGN=4
			BCJ=--arm
		fi
		;;

	arm64)
		ALIGN=4

		# ARM64 filter was added in XZ Utils 5.4.0.
		if [ "$XZ_VERSION" -ge 50040002 ]; then
			BCJ=--arm64
		else
			echo "$0: Upgrading to xz >= 5.4.0" \
				"would enable the ARM64 filter" \
				"for better compression" >&2
		fi
		;;

	csky)
		ALIGN=2
		;;

	loongarch)
		ALIGN=4
		;;

	mips)
		if is_enabled CONFIG_CPU_MICROMIPS; then
			ALIGN=2
		else
			ALIGN=4
		fi
		;;

	parisc)
		ALIGN=4
		;;

	powerpc)
		ALIGN=4

		# The filter is only for big endian instruction encoding.
		if is_enabled CONFIG_CPU_BIG_ENDIAN; then
			BCJ=--powerpc
		fi
		;;

	riscv)
		if is_enabled CONFIG_RISCV_ISA_C; then
			ALIGN=2
		else
			ALIGN=4
		fi

		# RISC-V filter was added in XZ Utils 5.6.0.
		if [ "$XZ_VERSION" -ge 50060002 ]; then
			BCJ=--riscv
		else
			echo "$0: Upgrading to xz >= 5.6.0" \
				"would enable the RISC-V filter" \
				"for better compression" >&2
		fi
		;;

	s390)
		ALIGN=2
		;;

	sh)
		ALIGN=2
		;;

	sparc)
		ALIGN=4
		BCJ=--sparc
		;;

	x86)
		ALIGN=1
		BCJ=--x86
		;;

	*)
		echo "$0: Arch-specific tuning is missing for '$SRCARCH'" >&2

		# Guess 2-byte-aligned instructions. Guessing too low
		# should hurt less than guessing too high.
		ALIGN=2
		;;
esac

# Select the LZMA2 options matching the instruction alignment.
case $ALIGN in
	1)  LZMA2OPTS= ;;
	2)  LZMA2OPTS=lp=1 ;;
	4)  LZMA2OPTS=lp=2,lc=2 ;;
	*)  echo "$0: ALIGN wrong or missing" >&2; exit 1 ;;
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

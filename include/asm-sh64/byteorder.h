#ifndef __ASM_SH64_BYTEORDER_H
#define __ASM_SH64_BYTEORDER_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * include/asm-sh64/byteorder.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 *
 */

#include <asm/types.h>

static __inline__ __const__ __u32 ___arch__swab32(__u32 x)
{
	__asm__("byterev	%0, %0\n\t"
		"shari		%0, 32, %0"
		: "=r" (x)
		: "0" (x));
	return x;
}

static __inline__ __const__ __u16 ___arch__swab16(__u16 x)
{
	__asm__("byterev	%0, %0\n\t"
		"shari		%0, 48, %0"
		: "=r" (x)
		: "0" (x));
	return x;
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#ifdef __LITTLE_ENDIAN__
#include <linux/byteorder/little_endian.h>
#else
#include <linux/byteorder/big_endian.h>
#endif

#endif /* __ASM_SH64_BYTEORDER_H */

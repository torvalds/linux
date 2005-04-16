/*
 * include/asm-v850/byteorder.h -- Endian id and conversion ops
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_BYTEORDER_H__
#define __V850_BYTEORDER_H__

#include <asm/types.h>
#include <linux/compiler.h>

#ifdef __GNUC__

static __inline__ __attribute_const__ __u32 ___arch__swab32 (__u32 word)
{
	__u32 res;
	__asm__ ("bsw %1, %0" : "=r" (res) : "r" (word));
	return res;
}

static __inline__ __attribute_const__ __u16 ___arch__swab16 (__u16 half_word)
{
	__u16 res;
	__asm__ ("bsh %1, %0" : "=r" (res) : "r" (half_word));
	return res;
}

#define __arch__swab32(x) ___arch__swab32(x)
#define __arch__swab16(x) ___arch__swab16(x)

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#endif /* __GNUC__ */

#include <linux/byteorder/little_endian.h>

#endif /* __V850_BYTEORDER_H__ */

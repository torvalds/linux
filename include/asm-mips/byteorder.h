/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 99, 2003 by Ralf Baechle
 */
#ifndef _ASM_BYTEORDER_H
#define _ASM_BYTEORDER_H

#include <linux/compiler.h>
#include <asm/types.h>

#ifdef __GNUC__

#ifdef CONFIG_CPU_MIPSR2

static __inline__ __attribute_const__ __u16 ___arch__swab16(__u16 x)
{
	__asm__(
	"	wsbh	%0, %1			\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch__swab16(x)	___arch__swab16(x)

static __inline__ __attribute_const__ __u32 ___arch__swab32(__u32 x)
{
	__asm__(
	"	wsbh	%0, %1			\n"
	"	rotr	%0, %0, 16		\n"
	: "=r" (x)
	: "r" (x));

	return x;
}
#define __arch__swab32(x)	___arch__swab32(x)

#ifdef CONFIG_CPU_MIPS64_R2

static __inline__ __attribute_const__ __u64 ___arch__swab64(__u64 x)
{
	__asm__(
	"	dsbh	%0, %1			\n"
	"	dshd	%0, %0			\n"
	"	drotr	%0, %0, 32		\n"
	: "=r" (x)
	: "r" (x));

	return x;
}

#define __arch__swab64(x)	___arch__swab64(x)

#endif /* CONFIG_CPU_MIPS64_R2 */

#endif /* CONFIG_CPU_MIPSR2 */

#if !defined(__STRICT_ANSI__) || defined(__KERNEL__)
#  define __BYTEORDER_HAS_U64__
#  define __SWAB_64_THRU_32__
#endif

#endif /* __GNUC__ */

#if defined(__MIPSEB__)
#  include <linux/byteorder/big_endian.h>
#elif defined(__MIPSEL__)
#  include <linux/byteorder/little_endian.h>
#else
#  error "MIPS, but neither __MIPSEB__, nor __MIPSEL__???"
#endif

#endif /* _ASM_BYTEORDER_H */

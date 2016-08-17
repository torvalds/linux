/*
 * IDI,NTNU
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Copyright (C) 2009, 2010, Jorn Amundsen <jorn.amundsen@ntnu.no>
 *
 * C header file to determine compile machine byte order. Take care when cross
 * compiling.
 *
 * $Id: byteorder.h 517 2013-02-17 20:34:39Z joern $
 */
/*
 * Portions copyright (c) 2013, Saso Kiselkov, All rights reserved
 */

#ifndef _CRYPTO_EDONR_BYTEORDER_H
#define	_CRYPTO_EDONR_BYTEORDER_H


#include <sys/param.h>

#if defined(__BYTE_ORDER)
#if (__BYTE_ORDER == __BIG_ENDIAN)
#define	MACHINE_IS_BIG_ENDIAN
#elif (__BYTE_ORDER == __LITTLE_ENDIAN)
#define	MACHINE_IS_LITTLE_ENDIAN
#endif
#elif defined(BYTE_ORDER)
#if (BYTE_ORDER == BIG_ENDIAN)
#define	MACHINE_IS_BIG_ENDIAN
#elif (BYTE_ORDER == LITTLE_ENDIAN)
#define	MACHINE_IS_LITTLE_ENDIAN
#endif
#endif /* __BYTE_ORDER || BYTE_ORDER */

#if !defined(MACHINE_IS_BIG_ENDIAN) && !defined(MACHINE_IS_LITTLE_ENDIAN)
#if defined(_BIG_ENDIAN) || defined(_MIPSEB)
#define	MACHINE_IS_BIG_ENDIAN
#endif
#if defined(_LITTLE_ENDIAN) || defined(_MIPSEL)
#define	MACHINE_IS_LITTLE_ENDIAN
#endif
#endif /* !MACHINE_IS_BIG_ENDIAN && !MACHINE_IS_LITTLE_ENDIAN */

#if !defined(MACHINE_IS_BIG_ENDIAN) && !defined(MACHINE_IS_LITTLE_ENDIAN)
#error unknown machine byte sex
#endif

#define	BYTEORDER_INCLUDED

#if defined(MACHINE_IS_BIG_ENDIAN)
/*
 * Byte swapping macros for big endian architectures and compilers,
 * add as appropriate for other architectures and/or compilers.
 *
 *     ld_swap64(src,dst) : uint64_t dst = *(src)
 *     st_swap64(src,dst) : *(dst)       = uint64_t src
 */

#if defined(__PPC__) || defined(_ARCH_PPC)

#if defined(__64BIT__)
#if defined(_ARCH_PWR7)
#define	aix_ld_swap64(s64, d64)\
	__asm__("ldbrx %0,0,%1" : "=r"(d64) : "r"(s64))
#define	aix_st_swap64(s64, d64)\
	__asm__ volatile("stdbrx %1,0,%0" : : "r"(d64), "r"(s64))
#else
#define	aix_ld_swap64(s64, d64)						\
{									\
	uint64_t *s4 = 0, h; /* initialize to zero for gcc warning */	\
									\
	__asm__("addi %0,%3,4;lwbrx %1,0,%3;lwbrx %2,0,%0;rldimi %1,%2,32,0"\
		: "+r"(s4), "=r"(d64), "=r"(h) : "b"(s64));		\
}

#define	aix_st_swap64(s64, d64)						\
{									\
	uint64_t *s4 = 0, h; /* initialize to zero for gcc warning */	\
	h = (s64) >> 32;						\
	__asm__ volatile("addi %0,%3,4;stwbrx %1,0,%3;stwbrx %2,0,%0"	\
		: "+r"(s4) : "r"(s64), "r"(h), "b"(d64));		\
}
#endif /* 64BIT && PWR7 */
#else
#define	aix_ld_swap64(s64, d64)						\
{									\
	uint32_t *s4 = 0, h, l;	/* initialize to zero for gcc warning */\
	__asm__("addi %0,%3,4;lwbrx %1,0,%3;lwbrx %2,0,%0"		\
		: "+r"(s4), "=r"(l), "=r"(h) : "b"(s64));		\
	d64 = ((uint64_t)h<<32) | l;					\
}

#define	aix_st_swap64(s64, d64)						\
{									\
	uint32_t *s4 = 0, h, l; /* initialize to zero for gcc warning */\
	l = (s64) & 0xfffffffful, h = (s64) >> 32;			\
	__asm__ volatile("addi %0,%3,4;stwbrx %1,0,%3;stwbrx %2,0,%0"	\
		: "+r"(s4) : "r"(l), "r"(h), "b"(d64));			\
}
#endif /* __64BIT__ */
#define	aix_ld_swap32(s32, d32)\
	__asm__("lwbrx %0,0,%1" : "=r"(d32) : "r"(s32))
#define	aix_st_swap32(s32, d32)\
	__asm__ volatile("stwbrx %1,0,%0" : : "r"(d32), "r"(s32))
#define	ld_swap32(s, d) aix_ld_swap32(s, d)
#define	st_swap32(s, d) aix_st_swap32(s, d)
#define	ld_swap64(s, d) aix_ld_swap64(s, d)
#define	st_swap64(s, d) aix_st_swap64(s, d)
#endif /* __PPC__ || _ARCH_PPC */

#if defined(__sparc)
#if !defined(__arch64__) && !defined(__sparcv8) && defined(__sparcv9)
#define	__arch64__
#endif
#if defined(__GNUC__) || (defined(__SUNPRO_C) && __SUNPRO_C > 0x590)
/* need Sun Studio C 5.10 and above for GNU inline assembly */
#if defined(__arch64__)
#define	sparc_ld_swap64(s64, d64)					\
	__asm__("ldxa [%1]0x88,%0" : "=r"(d64) : "r"(s64))
#define	sparc_st_swap64(s64, d64)					\
	__asm__ volatile("stxa %0,[%1]0x88" : : "r"(s64), "r"(d64))
#define	st_swap64(s, d) sparc_st_swap64(s, d)
#else
#define	sparc_ld_swap64(s64, d64)					\
{									\
	uint32_t *s4, h, l;						\
	__asm__("add %3,4,%0\n\tlda [%3]0x88,%1\n\tlda [%0]0x88,%2"	\
		: "+r"(s4), "=r"(l), "=r"(h) : "r"(s64));		\
	d64 = ((uint64_t)h<<32) | l;					\
}
#define	sparc_st_swap64(s64, d64)					\
{									\
	uint32_t *s4, h, l;						\
	l = (s64) & 0xfffffffful, h = (s64) >> 32;			\
	__asm__ volatile("add %3,4,%0\n\tsta %1,[%3]0x88\n\tsta %2,[%0]0x88"\
		: "+r"(s4) : "r"(l), "r"(h), "r"(d64));			\
}
#endif /* sparc64 */
#define	sparc_ld_swap32(s32, d32)\
	__asm__("lda [%1]0x88,%0" : "=r"(d32) : "r"(s32))
#define	sparc_st_swap32(s32, d32)\
	__asm__ volatile("sta %0,[%1]0x88" : : "r"(s32), "r"(d32))
#define	ld_swap32(s, d) sparc_ld_swap32(s, d)
#define	st_swap32(s, d) sparc_st_swap32(s, d)
#define	ld_swap64(s, d) sparc_ld_swap64(s, d)
#define	st_swap64(s, d) sparc_st_swap64(s, d)
#endif /* GCC || Sun Studio C > 5.9 */
#endif /* sparc */

/* GCC fallback */
#if ((__GNUC__ >= 4) || defined(__PGIC__)) && !defined(ld_swap32)
#define	ld_swap32(s, d) (d = __builtin_bswap32(*(s)))
#define	st_swap32(s, d) (*(d) = __builtin_bswap32(s))
#endif /* GCC4/PGIC && !swap32 */
#if ((__GNUC__ >= 4) || defined(__PGIC__)) && !defined(ld_swap64)
#define	ld_swap64(s, d) (d = __builtin_bswap64(*(s)))
#define	st_swap64(s, d) (*(d) = __builtin_bswap64(s))
#endif /* GCC4/PGIC && !swap64 */

/* generic fallback */
#if !defined(ld_swap32)
#define	ld_swap32(s, d)							\
	(d = (*(s) >> 24) | (*(s) >> 8 & 0xff00) |			\
	(*(s) << 8 & 0xff0000) | (*(s) << 24))
#define	st_swap32(s, d)							\
	(*(d) = ((s) >> 24) | ((s) >> 8 & 0xff00) |			\
	((s) << 8 & 0xff0000) | ((s) << 24))
#endif
#if !defined(ld_swap64)
#define	ld_swap64(s, d)							\
	(d = (*(s) >> 56) | (*(s) >> 40 & 0xff00) |			\
	(*(s) >> 24 & 0xff0000) | (*(s) >> 8 & 0xff000000) |		\
	(*(s) & 0xff000000) << 8 | (*(s) & 0xff0000) << 24 |		\
	(*(s) & 0xff00) << 40 | *(s) << 56)
#define	st_swap64(s, d)							\
	(*(d) = ((s) >> 56) | ((s) >> 40 & 0xff00) |			\
	((s) >> 24 & 0xff0000) | ((s) >> 8 & 0xff000000) |		\
	((s) & 0xff000000) << 8 | ((s) & 0xff0000) << 24 |		\
	((s) & 0xff00) << 40 | (s) << 56)
#endif

#endif /* MACHINE_IS_BIG_ENDIAN */


#if defined(MACHINE_IS_LITTLE_ENDIAN)
/* replace swaps with simple assignments on little endian systems */
#undef	ld_swap32
#undef	st_swap32
#define	ld_swap32(s, d) (d = *(s))
#define	st_swap32(s, d) (*(d) = s)
#undef	ld_swap64
#undef	st_swap64
#define	ld_swap64(s, d) (d = *(s))
#define	st_swap64(s, d) (*(d) = s)
#endif /* MACHINE_IS_LITTLE_ENDIAN */

#endif /* _CRYPTO_EDONR_BYTEORDER_H */

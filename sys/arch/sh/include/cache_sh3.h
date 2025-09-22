/*	$OpenBSD: cache_sh3.h,v 1.2 2008/06/26 05:42:12 ray Exp $	*/
/*	$NetBSD: cache_sh3.h,v 1.8 2006/03/04 01:55:03 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * SH3: SH7708, SH7708S, SH7708R, SH7709, SH7709A
 */
#ifndef _SH_CACHE_SH3_H_
#define	_SH_CACHE_SH3_H_
#include <sh/devreg.h>
#ifdef _KERNEL

#define	SH3_CCR			0xffffffec
#define	  SH3_CCR_CE		  0x00000001
#define	  SH3_CCR_WT		  0x00000002
/* SH7708 don't have CB bit */
#define	  SH3_CCR_CB		  0x00000004
#define	  SH3_CCR_CF		  0x00000008
/* SH7709A don't have RA bit */
#define	  SH3_CCR_RA		  0x00000020

/* SH7709A specific cache-lock control register */
#define	SH7709A_CCR2		0xa40000b0
#define	  SH7709A_CCR2_W2LOCK	  0x00000001
#define	  SH7709A_CCR2_W2LOAD	  0x00000002
#define	  SH7709A_CCR2_W3LOCK	  0x00000100
#define	  SH7709A_CCR2_W3LOAD	  0x00000200

#define	SH3_CCA			0xf0000000
/* Address specification */
#define	  CCA_A			  0x00000008
#define	  CCA_ENTRY_SHIFT	  4
/* 8KB cache (SH7708, SH7708S, SH7708R, SH7709) */
#define	  CCA_8K_ENTRY		  128
#define	  CCA_8K_ENTRY_MASK	  0x000007f0	/* [10:4] */
#define	  CCA_8K_WAY_SHIFT	  11
#define	  CCA_8K_WAY_MASK	  0x00001800	/* [12:11] */
/* 16KB cache (SH7709A) */
#define	  CCA_16K_ENTRY		  256
#define	  CCA_16K_ENTRY_MASK	  0x00000ff0	/* [11:4] */
#define	  CCA_16K_WAY_SHIFT	  12
#define	  CCA_16K_WAY_MASK	  0x00003000	/* [13:12] */

/* Data specification */
#define	  CCA_V			  0x00000001
#define	  CCA_U			  0x00000002
#define	  CCA_LRU_SHIFT		  4
#define	  CCA_LRU_MASK		  0x000003f0	/* [9:4] */
#define	  CCA_TAGADDR_SHIFT	  10
#define	  CCA_TAGADDR_MASK	  0xfffffc00	/* [31:10] */

#define	SH3_CCD			0xf1000000
/* Address specification */
#define	  CCD_L_SHIFT		  2
#define	  CCD_L_MASK		  0x0000000c	/* [3:2] */
#define	  CCD_E_SHIFT		  4
#define	  CCD_8K_E_MASK		  0x000007f0	/* [10:4] */
#define	  CCD_16K_E_MASK	  0x00000ff0	/* [11:4] */
#define	  CCD_8K_W_SHIFT	  11
#define	  CCD_8K_W_MASK		  0x00001800	/* [12:11] */
#define	  CCD_16K_W_SHIFT	  12
#define	  CCD_16K_W_MASK	  0x00003000	/* [13:12] */
/* Data specification */

/*
 * Configuration
 */
#define	SH3_CACHE_LINESZ		16
#define	SH3_CACHE_NORMAL_WAY		4
#define	SH3_CACHE_RAMMODE_WAY		2

#define	SH3_CACHE_8K_ENTRY		128
#define	SH3_CACHE_8K_WAY_NORMAL		4
#define	SH3_CACHE_8K_WAY_RAMMODE	2

#define	SH3_CACHE_16K_ENTRY		256
#define	SH3_CACHE_16K_WAY		4

/*
 * cache flush macro for locore level code.
 */
#define	SH3_CACHE_8K_FLUSH(maxway)					\
do {									\
	uint32_t __e, __w, __wa, __a;					\
									\
	for (__w = 0; __w < maxway; __w++) {				\
		__wa = SH3_CCA | __w << CCA_8K_WAY_SHIFT;		\
		for (__e = 0; __e < CCA_8K_ENTRY; __e++)	{	\
			__a = __wa |(__e << CCA_ENTRY_SHIFT);		\
			(*(volatile uint32_t *)__a) &=		\
				~(CCA_U | CCA_V);			\
		}							\
	}								\
} while (/*CONSTCOND*/0)

#define	SH3_CACHE_16K_FLUSH()						\
do {									\
	uint32_t __e, __w, __wa, __a;					\
									\
	for (__w = 0; __w < SH3_CACHE_16K_WAY; __w++) {			\
		__wa = SH3_CCA | __w << CCA_16K_WAY_SHIFT;		\
		for (__e = 0; __e < CCA_16K_ENTRY; __e++)	{	\
			__a = __wa |(__e << CCA_ENTRY_SHIFT);		\
			(*(volatile uint32_t *)__a) &=		\
				~(CCA_U | CCA_V);			\
		}							\
	}								\
} while (/*CONSTCOND*/0)

#define	SH7708_CACHE_FLUSH()		SH3_CACHE_8K_FLUSH(4)
#define	SH7708_CACHE_FLUSH_RAMMODE()	SH3_CACHE_8K_FLUSH(2)
#define	SH7708S_CACHE_FLUSH()		SH3_CACHE_8K_FLUSH(4)
#define	SH7708S_CACHE_FLUSH_RAMMODE()	SH3_CACHE_8K_FLUSH(2)
#define	SH7708R_CACHE_FLUSH()		SH3_CACHE_8K_FLUSH(4)
#define	SH7708R_CACHE_FLUSH_RAMMODE()	SH3_CACHE_8K_FLUSH(2)
#define	SH7709_CACHE_FLUSH()		SH3_CACHE_8K_FLUSH(4)
#define	SH7709_CACHE_FLUSH_RAMMODE()	SH3_CACHE_8K_FLUSH(2)
#define	SH7709A_CACHE_FLUSH()		SH3_CACHE_16K_FLUSH()

#ifndef _LOCORE
extern void sh3_cache_config(void);
#endif
#endif /* _KERNEL */
#endif /* !_SH_CACHE_SH3_H_ */

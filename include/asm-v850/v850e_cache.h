/*
 * include/asm-v850/v850e_cache.h -- Cache control for V850E cache memories
 *
 *  Copyright (C) 2001,03  NEC Electronics Corporation
 *  Copyright (C) 2001,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

/* This file implements cache control for the rather simple cache used on
   some V850E CPUs, specifically the NB85E/TEG CPU-core and the V850E/ME2
   CPU.  V850E2 processors have their own (better) cache
   implementation.  */

#ifndef __V850_V850E_CACHE_H__
#define __V850_V850E_CACHE_H__

#include <asm/types.h>


/* Cache control registers.  */
#define V850E_CACHE_BHC_ADDR	0xFFFFF06A
#define V850E_CACHE_BHC		(*(volatile u16 *)V850E_CACHE_BHC_ADDR)
#define V850E_CACHE_ICC_ADDR	0xFFFFF070
#define V850E_CACHE_ICC		(*(volatile u16 *)V850E_CACHE_ICC_ADDR)
#define V850E_CACHE_ISI_ADDR	0xFFFFF072
#define V850E_CACHE_ISI		(*(volatile u16 *)V850E_CACHE_ISI_ADDR)
#define V850E_CACHE_DCC_ADDR	0xFFFFF078
#define V850E_CACHE_DCC		(*(volatile u16 *)V850E_CACHE_DCC_ADDR)

/* Size of a cache line in bytes.  */
#define V850E_CACHE_LINE_SIZE	16

/* For <asm/cache.h> */
#define L1_CACHE_BYTES		V850E_CACHE_LINE_SIZE


#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
/* Set caching params via the BHC, ICC, and DCC registers.  */
void v850e_cache_enable (u16 bhc, u16 icc, u16 dcc);
#endif /* __KERNEL__ && !__ASSEMBLY__ */


#endif /* __V850_V850E_CACHE_H__ */

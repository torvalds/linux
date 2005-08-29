/*
 * include/asm-v850/v850e2_cache_cache.h -- Cache control for V850E2
 * 	cache memories
 *
 *  Copyright (C) 2003,05  NEC Electronics Corporation
 *  Copyright (C) 2003,05  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_V850E2_CACHE_H__
#define __V850_V850E2_CACHE_H__

#include <asm/types.h>


/* Cache control registers.  */

/* Bus Transaction Control */
#define V850E2_CACHE_BTSC_ADDR	0xFFFFF070
#define V850E2_CACHE_BTSC 	(*(volatile u16 *)V850E2_CACHE_BTSC_ADDR)
#define V850E2_CACHE_BTSC_ICM	0x0001 /* icache enable */
#define V850E2_CACHE_BTSC_DCM0	0x0004 /* dcache enable, bit 0 */
#define V850E2_CACHE_BTSC_DCM1	0x0008 /* dcache enable, bit 1 */
#define V850E2_CACHE_BTSC_DCM_WT		      /* write-through */ \
			V850E2_CACHE_BTSC_DCM0
#ifdef CONFIG_V850E2_V850E2S
# define V850E2_CACHE_BTSC_DCM_WB_NO_ALLOC    /* write-back, non-alloc */ \
			V850E2_CACHE_BTSC_DCM1	
# define V850E2_CACHE_BTSC_DCM_WB_ALLOC	      /* write-back, non-alloc */ \
			(V850E2_CACHE_BTSC_DCM1 | V850E2_CACHE_BTSC_DCM0)
# define V850E2_CACHE_BTSC_ISEQ	0x0010 /* icache `address sequence mode' */
# define V850E2_CACHE_BTSC_DSEQ	0x0020 /* dcache `address sequence mode' */
# define V850E2_CACHE_BTSC_IRFC	0x0030
# define V850E2_CACHE_BTSC_ILCD	0x4000
# define V850E2_CACHE_BTSC_VABE	0x8000
#endif /* CONFIG_V850E2_V850E2S */

/* Cache operation start address register (low-bits).  */
#define V850E2_CACHE_CADL_ADDR	0xFFFFF074
#define V850E2_CACHE_CADL 	(*(volatile u16 *)V850E2_CACHE_CADL_ADDR)
/* Cache operation start address register (high-bits).  */
#define V850E2_CACHE_CADH_ADDR	0xFFFFF076
#define V850E2_CACHE_CADH 	(*(volatile u16 *)V850E2_CACHE_CADH_ADDR)
/* Cache operation count register.  */
#define V850E2_CACHE_CCNT_ADDR	0xFFFFF078
#define V850E2_CACHE_CCNT 	(*(volatile u16 *)V850E2_CACHE_CCNT_ADDR)
/* Cache operation specification register.  */
#define V850E2_CACHE_COPR_ADDR	0xFFFFF07A
#define V850E2_CACHE_COPR 	(*(volatile u16 *)V850E2_CACHE_COPR_ADDR)
#define V850E2_CACHE_COPR_STRT	0x0001 /* start cache operation */
#define V850E2_CACHE_COPR_LBSL	0x0100 /* 0 = icache, 1 = dcache */
#define V850E2_CACHE_COPR_WSLE	0x0200 /* operate on cache way */
#define V850E2_CACHE_COPR_WSL(way) ((way) * 0x0400) /* way select */
#define V850E2_CACHE_COPR_CFC(op)  ((op)  * 0x1000) /* cache function code */


/* Size of a cache line in bytes.  */
#define V850E2_CACHE_LINE_SIZE_BITS	4
#define V850E2_CACHE_LINE_SIZE		(1 << V850E2_CACHE_LINE_SIZE_BITS)

/* The size of each cache `way' in lines.  */
#define V850E2_CACHE_WAY_SIZE		256


/* For <asm/cache.h> */
#define L1_CACHE_BYTES			V850E2_CACHE_LINE_SIZE
#define L1_CACHE_SHIFT			V850E2_CACHE_LINE_SIZE_BITS


#endif /* __V850_V850E2_CACHE_H__ */

/*
 * include/asm-sh/cpu-sh3/cache.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH3_CACHE_H
#define __ASM_CPU_SH3_CACHE_H

#define L1_CACHE_SHIFT	4

#define CCR		0xffffffec	/* Address of Cache Control Register */

#define CCR_CACHE_CE	0x01	/* Cache Enable */
#define CCR_CACHE_WT	0x02	/* Write-Through (for P0,U0,P3) (else writeback) */
#define CCR_CACHE_CB	0x04	/* Write-Back (for P1) (else writethrough) */
#define CCR_CACHE_CF	0x08	/* Cache Flush */
#define CCR_CACHE_ORA	0x20	/* RAM mode */

#define CACHE_OC_ADDRESS_ARRAY	0xf0000000
#define CACHE_PHYSADDR_MASK	0x1ffffc00

#define CCR_CACHE_ENABLE	CCR_CACHE_CE
#define CCR_CACHE_INVALIDATE	CCR_CACHE_CF

#if defined(CONFIG_CPU_SUBTYPE_SH7705) || defined(CONFIG_CPU_SUBTYPE_SH7710)
#define CCR3	0xa40000b4
#define CCR_CACHE_16KB  0x00010000
#define CCR_CACHE_32KB	0x00020000
#endif

#endif /* __ASM_CPU_SH3_CACHE_H */

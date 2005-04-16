/*
 * include/asm-sh/cpu-sh2/cache.h
 *
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH2_CACHE_H
#define __ASM_CPU_SH2_CACHE_H

#define L1_CACHE_SHIFT	4

#define CCR		0xfffffe92	/* Address of Cache Control Register */

#define CCR_CACHE_CE	0x01	/* Cache enable */
#define CCR_CACHE_ID	0x02	/* Instruction Replacement disable */
#define CCR_CACHE_OD	0x04	/* Data Replacement disable */
#define CCR_CACHE_TW	0x08	/* Two-way mode */
#define CCR_CACHE_CP	0x10	/* Cache purge */

#define CACHE_OC_ADDRESS_ARRAY	0x60000000

#define CCR_CACHE_ENABLE	CCR_CACHE_CE
#define CCR_CACHE_INVALIDATE	CCR_CACHE_CP
#define CCR_CACHE_ORA		CCR_CACHE_TW
#define CCR_CACHE_WT		0x00	/* SH-2 is _always_ write-through */

#endif /* __ASM_CPU_SH2_CACHE_H */


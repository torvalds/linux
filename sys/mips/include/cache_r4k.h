/*	$NetBSD: cache_r4k.h,v 1.10 2003/03/08 04:43:26 rafal Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project by
 *	Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Cache definitions/operations for R4000-style caches.
 */

#define	CACHE_R4K_I			0
#define	CACHE_R4K_D			1
#define	CACHE_R4K_SI			2
#define	CACHE_R4K_SD			3

#define	CACHEOP_R4K_INDEX_INV		(0 << 2)	/* I, SI */
#define	CACHEOP_R4K_INDEX_WB_INV	(0 << 2)	/* D, SD */
#define	CACHEOP_R4K_INDEX_LOAD_TAG	(1 << 2)	/* all */
#define	CACHEOP_R4K_INDEX_STORE_TAG	(2 << 2)	/* all */
#define	CACHEOP_R4K_CREATE_DIRTY_EXCL	(3 << 2)	/* D, SD */
#define	CACHEOP_R4K_HIT_INV		(4 << 2)	/* all */
#define	CACHEOP_R4K_HIT_WB_INV		(5 << 2)	/* D, SD */
#define	CACHEOP_R4K_FILL		(5 << 2)	/* I */
#define	CACHEOP_R4K_HIT_WB		(6 << 2)	/* I, D, SD */
#define	CACHEOP_R4K_HIT_SET_VIRTUAL	(7 << 2)	/* SI, SD */

#if !defined(LOCORE)

/*
 * cache_r4k_op_line:
 *
 *	Perform the specified cache operation on a single line.
 */
#define	cache_op_r4k_line(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0(%0)				\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_8lines_16:
 *
 *	Perform the specified cache operation on 8 16-byte cache lines.
 */
#define	cache_r4k_op_8lines_16(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x00(%0); cache %1, 0x10(%0)		\n\t"	\
		"cache %1, 0x20(%0); cache %1, 0x30(%0)		\n\t"	\
		"cache %1, 0x40(%0); cache %1, 0x50(%0)		\n\t"	\
		"cache %1, 0x60(%0); cache %1, 0x70(%0)		\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_8lines_32:
 *
 *	Perform the specified cache operation on 8 32-byte cache lines.
 */
#define	cache_r4k_op_8lines_32(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x00(%0); cache %1, 0x20(%0)		\n\t"	\
		"cache %1, 0x40(%0); cache %1, 0x60(%0)		\n\t"	\
		"cache %1, 0x80(%0); cache %1, 0xa0(%0)		\n\t"	\
		"cache %1, 0xc0(%0); cache %1, 0xe0(%0)		\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_8lines_64:
 *
 *	Perform the specified cache operation on 8 64-byte cache lines.
 */
#define	cache_r4k_op_8lines_64(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x000(%0); cache %1, 0x040(%0)	\n\t"	\
		"cache %1, 0x080(%0); cache %1, 0x0c0(%0)	\n\t"	\
		"cache %1, 0x100(%0); cache %1, 0x140(%0)	\n\t"	\
		"cache %1, 0x180(%0); cache %1, 0x1c0(%0)	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_32lines_16:
 *
 *	Perform the specified cache operation on 32 16-byte
 *	cache lines.
 */
#define	cache_r4k_op_32lines_16(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x000(%0); cache %1, 0x010(%0);	\n\t"	\
		"cache %1, 0x020(%0); cache %1, 0x030(%0);	\n\t"	\
		"cache %1, 0x040(%0); cache %1, 0x050(%0);	\n\t"	\
		"cache %1, 0x060(%0); cache %1, 0x070(%0);	\n\t"	\
		"cache %1, 0x080(%0); cache %1, 0x090(%0);	\n\t"	\
		"cache %1, 0x0a0(%0); cache %1, 0x0b0(%0);	\n\t"	\
		"cache %1, 0x0c0(%0); cache %1, 0x0d0(%0);	\n\t"	\
		"cache %1, 0x0e0(%0); cache %1, 0x0f0(%0);	\n\t"	\
		"cache %1, 0x100(%0); cache %1, 0x110(%0);	\n\t"	\
		"cache %1, 0x120(%0); cache %1, 0x130(%0);	\n\t"	\
		"cache %1, 0x140(%0); cache %1, 0x150(%0);	\n\t"	\
		"cache %1, 0x160(%0); cache %1, 0x170(%0);	\n\t"	\
		"cache %1, 0x180(%0); cache %1, 0x190(%0);	\n\t"	\
		"cache %1, 0x1a0(%0); cache %1, 0x1b0(%0);	\n\t"	\
		"cache %1, 0x1c0(%0); cache %1, 0x1d0(%0);	\n\t"	\
		"cache %1, 0x1e0(%0); cache %1, 0x1f0(%0);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_32lines_32:
 *
 *	Perform the specified cache operation on 32 32-byte
 *	cache lines.
 */
#define	cache_r4k_op_32lines_32(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x000(%0); cache %1, 0x020(%0);	\n\t"	\
		"cache %1, 0x040(%0); cache %1, 0x060(%0);	\n\t"	\
		"cache %1, 0x080(%0); cache %1, 0x0a0(%0);	\n\t"	\
		"cache %1, 0x0c0(%0); cache %1, 0x0e0(%0);	\n\t"	\
		"cache %1, 0x100(%0); cache %1, 0x120(%0);	\n\t"	\
		"cache %1, 0x140(%0); cache %1, 0x160(%0);	\n\t"	\
		"cache %1, 0x180(%0); cache %1, 0x1a0(%0);	\n\t"	\
		"cache %1, 0x1c0(%0); cache %1, 0x1e0(%0);	\n\t"	\
		"cache %1, 0x200(%0); cache %1, 0x220(%0);	\n\t"	\
		"cache %1, 0x240(%0); cache %1, 0x260(%0);	\n\t"	\
		"cache %1, 0x280(%0); cache %1, 0x2a0(%0);	\n\t"	\
		"cache %1, 0x2c0(%0); cache %1, 0x2e0(%0);	\n\t"	\
		"cache %1, 0x300(%0); cache %1, 0x320(%0);	\n\t"	\
		"cache %1, 0x340(%0); cache %1, 0x360(%0);	\n\t"	\
		"cache %1, 0x380(%0); cache %1, 0x3a0(%0);	\n\t"	\
		"cache %1, 0x3c0(%0); cache %1, 0x3e0(%0);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_32lines_64:
 *
 *	Perform the specified cache operation on 32 64-byte
 *	cache lines.
 */
#define	cache_r4k_op_32lines_64(va, op)					\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x000(%0); cache %1, 0x040(%0);	\n\t"	\
		"cache %1, 0x080(%0); cache %1, 0x0c0(%0);	\n\t"	\
		"cache %1, 0x100(%0); cache %1, 0x140(%0);	\n\t"	\
		"cache %1, 0x180(%0); cache %1, 0x1c0(%0);	\n\t"	\
		"cache %1, 0x200(%0); cache %1, 0x240(%0);	\n\t"	\
		"cache %1, 0x280(%0); cache %1, 0x2c0(%0);	\n\t"	\
		"cache %1, 0x300(%0); cache %1, 0x340(%0);	\n\t"	\
		"cache %1, 0x380(%0); cache %1, 0x3c0(%0);	\n\t"	\
		"cache %1, 0x400(%0); cache %1, 0x440(%0);	\n\t"	\
		"cache %1, 0x480(%0); cache %1, 0x4c0(%0);	\n\t"	\
		"cache %1, 0x500(%0); cache %1, 0x540(%0);	\n\t"	\
		"cache %1, 0x580(%0); cache %1, 0x5c0(%0);	\n\t"	\
		"cache %1, 0x600(%0); cache %1, 0x640(%0);	\n\t"	\
		"cache %1, 0x680(%0); cache %1, 0x6c0(%0);	\n\t"	\
		"cache %1, 0x700(%0); cache %1, 0x740(%0);	\n\t"	\
		"cache %1, 0x780(%0); cache %1, 0x7c0(%0);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_32lines_128:
 *
 *	Perform the specified cache operation on 32 128-byte
 *	cache lines.
 */
#define	cache_r4k_op_32lines_128(va, op)				\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %1, 0x0000(%0); cache %1, 0x0080(%0);	\n\t"	\
		"cache %1, 0x0100(%0); cache %1, 0x0180(%0);	\n\t"	\
		"cache %1, 0x0200(%0); cache %1, 0x0280(%0);	\n\t"	\
		"cache %1, 0x0300(%0); cache %1, 0x0380(%0);	\n\t"	\
		"cache %1, 0x0400(%0); cache %1, 0x0480(%0);	\n\t"	\
		"cache %1, 0x0500(%0); cache %1, 0x0580(%0);	\n\t"	\
		"cache %1, 0x0600(%0); cache %1, 0x0680(%0);	\n\t"	\
		"cache %1, 0x0700(%0); cache %1, 0x0780(%0);	\n\t"	\
		"cache %1, 0x0800(%0); cache %1, 0x0880(%0);	\n\t"	\
		"cache %1, 0x0900(%0); cache %1, 0x0980(%0);	\n\t"	\
		"cache %1, 0x0a00(%0); cache %1, 0x0a80(%0);	\n\t"	\
		"cache %1, 0x0b00(%0); cache %1, 0x0b80(%0);	\n\t"	\
		"cache %1, 0x0c00(%0); cache %1, 0x0c80(%0);	\n\t"	\
		"cache %1, 0x0d00(%0); cache %1, 0x0d80(%0);	\n\t"	\
		"cache %1, 0x0e00(%0); cache %1, 0x0e80(%0);	\n\t"	\
		"cache %1, 0x0f00(%0); cache %1, 0x0f80(%0);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va), "i" (op)					\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_16lines_16_2way:
 *
 *	Perform the specified cache operation on 16 16-byte
 * 	cache lines, 2-ways.
 */
#define	cache_r4k_op_16lines_16_2way(va1, va2, op)			\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %2, 0x000(%0); cache %2, 0x000(%1);	\n\t"	\
		"cache %2, 0x010(%0); cache %2, 0x010(%1);	\n\t"	\
		"cache %2, 0x020(%0); cache %2, 0x020(%1);	\n\t"	\
		"cache %2, 0x030(%0); cache %2, 0x030(%1);	\n\t"	\
		"cache %2, 0x040(%0); cache %2, 0x040(%1);	\n\t"	\
		"cache %2, 0x050(%0); cache %2, 0x050(%1);	\n\t"	\
		"cache %2, 0x060(%0); cache %2, 0x060(%1);	\n\t"	\
		"cache %2, 0x070(%0); cache %2, 0x070(%1);	\n\t"	\
		"cache %2, 0x080(%0); cache %2, 0x080(%1);	\n\t"	\
		"cache %2, 0x090(%0); cache %2, 0x090(%1);	\n\t"	\
		"cache %2, 0x0a0(%0); cache %2, 0x0a0(%1);	\n\t"	\
		"cache %2, 0x0b0(%0); cache %2, 0x0b0(%1);	\n\t"	\
		"cache %2, 0x0c0(%0); cache %2, 0x0c0(%1);	\n\t"	\
		"cache %2, 0x0d0(%0); cache %2, 0x0d0(%1);	\n\t"	\
		"cache %2, 0x0e0(%0); cache %2, 0x0e0(%1);	\n\t"	\
		"cache %2, 0x0f0(%0); cache %2, 0x0f0(%1);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va1), "r" (va2), "i" (op)				\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_16lines_32_2way:
 *
 *	Perform the specified cache operation on 16 32-byte
 * 	cache lines, 2-ways.
 */
#define	cache_r4k_op_16lines_32_2way(va1, va2, op)			\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %2, 0x000(%0); cache %2, 0x000(%1);	\n\t"	\
		"cache %2, 0x020(%0); cache %2, 0x020(%1);	\n\t"	\
		"cache %2, 0x040(%0); cache %2, 0x040(%1);	\n\t"	\
		"cache %2, 0x060(%0); cache %2, 0x060(%1);	\n\t"	\
		"cache %2, 0x080(%0); cache %2, 0x080(%1);	\n\t"	\
		"cache %2, 0x0a0(%0); cache %2, 0x0a0(%1);	\n\t"	\
		"cache %2, 0x0c0(%0); cache %2, 0x0c0(%1);	\n\t"	\
		"cache %2, 0x0e0(%0); cache %2, 0x0e0(%1);	\n\t"	\
		"cache %2, 0x100(%0); cache %2, 0x100(%1);	\n\t"	\
		"cache %2, 0x120(%0); cache %2, 0x120(%1);	\n\t"	\
		"cache %2, 0x140(%0); cache %2, 0x140(%1);	\n\t"	\
		"cache %2, 0x160(%0); cache %2, 0x160(%1);	\n\t"	\
		"cache %2, 0x180(%0); cache %2, 0x180(%1);	\n\t"	\
		"cache %2, 0x1a0(%0); cache %2, 0x1a0(%1);	\n\t"	\
		"cache %2, 0x1c0(%0); cache %2, 0x1c0(%1);	\n\t"	\
		"cache %2, 0x1e0(%0); cache %2, 0x1e0(%1);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va1), "r" (va2), "i" (op)				\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_8lines_16_4way:
 *
 *	Perform the specified cache operation on 8 16-byte
 * 	cache lines, 4-ways.
 */
#define	cache_r4k_op_8lines_16_4way(va1, va2, va3, va4, op)		\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %4, 0x000(%0); cache %4, 0x000(%1);	\n\t"	\
		"cache %4, 0x000(%2); cache %4, 0x000(%3);	\n\t"	\
		"cache %4, 0x010(%0); cache %4, 0x010(%1);	\n\t"	\
		"cache %4, 0x010(%2); cache %4, 0x010(%3);	\n\t"	\
		"cache %4, 0x020(%0); cache %4, 0x020(%1);	\n\t"	\
		"cache %4, 0x020(%2); cache %4, 0x020(%3);	\n\t"	\
		"cache %4, 0x030(%0); cache %4, 0x030(%1);	\n\t"	\
		"cache %4, 0x030(%2); cache %4, 0x030(%3);	\n\t"	\
		"cache %4, 0x040(%0); cache %4, 0x040(%1);	\n\t"	\
		"cache %4, 0x040(%2); cache %4, 0x040(%3);	\n\t"	\
		"cache %4, 0x050(%0); cache %4, 0x050(%1);	\n\t"	\
		"cache %4, 0x050(%2); cache %4, 0x050(%3);	\n\t"	\
		"cache %4, 0x060(%0); cache %4, 0x060(%1);	\n\t"	\
		"cache %4, 0x060(%2); cache %4, 0x060(%3);	\n\t"	\
		"cache %4, 0x070(%0); cache %4, 0x070(%1);	\n\t"	\
		"cache %4, 0x070(%2); cache %4, 0x070(%3);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va1), "r" (va2), "r" (va3), "r" (va4), "i" (op)	\
	    : "memory");						\
} while (/*CONSTCOND*/0)

/*
 * cache_r4k_op_8lines_32_4way:
 *
 *	Perform the specified cache operation on 8 32-byte
 * 	cache lines, 4-ways.
 */
#define	cache_r4k_op_8lines_32_4way(va1, va2, va3, va4, op)		\
do {									\
	__asm __volatile(						\
		".set noreorder					\n\t"	\
		"cache %4, 0x000(%0); cache %4, 0x000(%1);	\n\t"	\
		"cache %4, 0x000(%2); cache %4, 0x000(%3);	\n\t"	\
		"cache %4, 0x020(%0); cache %4, 0x020(%1);	\n\t"	\
		"cache %4, 0x020(%2); cache %4, 0x020(%3);	\n\t"	\
		"cache %4, 0x040(%0); cache %4, 0x040(%1);	\n\t"	\
		"cache %4, 0x040(%2); cache %4, 0x040(%3);	\n\t"	\
		"cache %4, 0x060(%0); cache %4, 0x060(%1);	\n\t"	\
		"cache %4, 0x060(%2); cache %4, 0x060(%3);	\n\t"	\
		"cache %4, 0x080(%0); cache %4, 0x080(%1);	\n\t"	\
		"cache %4, 0x080(%2); cache %4, 0x080(%3);	\n\t"	\
		"cache %4, 0x0a0(%0); cache %4, 0x0a0(%1);	\n\t"	\
		"cache %4, 0x0a0(%2); cache %4, 0x0a0(%3);	\n\t"	\
		"cache %4, 0x0c0(%0); cache %4, 0x0c0(%1);	\n\t"	\
		"cache %4, 0x0c0(%2); cache %4, 0x0c0(%3);	\n\t"	\
		"cache %4, 0x0e0(%0); cache %4, 0x0e0(%1);	\n\t"	\
		"cache %4, 0x0e0(%2); cache %4, 0x0e0(%3);	\n\t"	\
		".set reorder"						\
	    :								\
	    : "r" (va1), "r" (va2), "r" (va3), "r" (va4), "i" (op)	\
	    : "memory");						\
} while (/*CONSTCOND*/0)

void	r4k_icache_sync_all_16(void);
void	r4k_icache_sync_range_16(vm_paddr_t, vm_size_t);
void	r4k_icache_sync_range_index_16(vm_paddr_t, vm_size_t);

void	r4k_icache_sync_all_32(void);
void	r4k_icache_sync_range_32(vm_paddr_t, vm_size_t);
void	r4k_icache_sync_range_index_32(vm_paddr_t, vm_size_t);

void	r4k_pdcache_wbinv_all_16(void);
void	r4k_pdcache_wbinv_range_16(vm_paddr_t, vm_size_t);
void	r4k_pdcache_wbinv_range_index_16(vm_paddr_t, vm_size_t);

void	r4k_pdcache_inv_range_16(vm_paddr_t, vm_size_t);
void	r4k_pdcache_wb_range_16(vm_paddr_t, vm_size_t);

void	r4k_pdcache_wbinv_all_32(void);
void	r4k_pdcache_wbinv_range_32(vm_paddr_t, vm_size_t);
void	r4k_pdcache_wbinv_range_index_32(vm_paddr_t, vm_size_t);

void	r4k_pdcache_inv_range_32(vm_paddr_t, vm_size_t);
void	r4k_pdcache_wb_range_32(vm_paddr_t, vm_size_t);

void	r4k_sdcache_wbinv_all_32(void);
void	r4k_sdcache_wbinv_range_32(vm_paddr_t, vm_size_t);
void	r4k_sdcache_wbinv_range_index_32(vm_paddr_t, vm_size_t);

void	r4k_sdcache_inv_range_32(vm_paddr_t, vm_size_t);
void	r4k_sdcache_wb_range_32(vm_paddr_t, vm_size_t);

void	r4k_sdcache_wbinv_all_128(void);
void	r4k_sdcache_wbinv_range_128(vm_paddr_t, vm_size_t);
void	r4k_sdcache_wbinv_range_index_128(vm_paddr_t, vm_size_t);

void	r4k_sdcache_inv_range_128(vm_paddr_t, vm_size_t);
void	r4k_sdcache_wb_range_128(vm_paddr_t, vm_size_t);

void	r4k_sdcache_wbinv_all_generic(void);
void	r4k_sdcache_wbinv_range_generic(vm_paddr_t, vm_size_t);
void	r4k_sdcache_wbinv_range_index_generic(vm_paddr_t, vm_size_t);

void	r4k_sdcache_inv_range_generic(vm_paddr_t, vm_size_t);
void	r4k_sdcache_wb_range_generic(vm_paddr_t, vm_size_t);

#endif /* !LOCORE */

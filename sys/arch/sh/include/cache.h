/*	$OpenBSD: cache.h,v 1.3 2008/06/26 05:42:12 ray Exp $	*/
/*	$NetBSD: cache.h,v 1.7 2006/01/21 00:46:36 uwe Exp $	*/

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
 * Cache configurations.
 *
 * SH3 I/D unified virtual-index physical-tag cache.
 * SH4 I/D separated virtual-index physical-tag cache.
 *
 *
 *         size       line-size entry way type
 * SH7708  4/8K       16B       128   2/4 P0,P2,U0 [1]
 *                                        P1 [2]
 * SH7709  4/8K       16B       128   2/4 [1]
 * SH7709A 16K        16B       256   4   [1]
 *
 * SH7750  I$  D$     line-size entry way
 *         8K  8/16K  32B       256   1   [1]
 * SH7750
 * SH7750S
 * SH7751  I$  D$     line-size entry way
 *         8K  8/16K  32B       256   1   [1]
 * 
 * SH7750R
 * SH7751R I$  D$     line-size entry way
 *         16K 16/32K 32B       512   2   [1]
 *
 * [1]	write-through/back selectable
 * [2]	write-through only
 *
 * Cache operations.
 *
 * There are some rules that must be followed:
 *
 *	I-cache Sync (all or range):
 *		The goal is to synchronize the instruction stream,
 *		so you may need to write-back dirty data cache
 *		blocks first.  If a range is requested, and you
 *		can't synchronize just a range, you have to hit
 *		the whole thing.
 *
 *	D-cache Write-back Invalidate range:
 *		If you can't WB-Inv a range, you must WB-Inv the
 *		entire D-cache.
 *
 *	D-cache Invalidate:
 *		If you can't Inv the D-cache without doing a
 *		Write-back, YOU MUST PANIC.  This is to catch
 *		errors in calling code.  Callers must be aware
 *		of this scenario, and must handle it appropriately
 *		(consider the bus_dma(9) operations).
 *
 *	D-cache Write-back:
 *		If you can't Write-back without doing an invalidate,
 *		that's fine.  Then treat this as a WB-Inv.  Skipping
 *		the invalidate is merely an optimization.
 *
 *	All operations:
 *		Valid virtual addresses must be passed to the
 *		cache operation.
 *
 *
 *	sh_icache_sync_all	Synchronize I-cache
 *
 *	sh_icache_sync_range	Synchronize I-cache range
 *
 *	sh_icache_sync_range_index (index ops)
 *
 *	sh_dcache_wbinv_all	Write-back Invalidate D-cache
 *
 *	sh_dcache_wbinv_range	Write-back Invalidate D-cache range
 *
 *	sh_dcache_wbinv_range_index (index ops)
 *
 *	sh_dcache_inv_range	Invalidate D-cache range
 *
 *	sh_dcache_wb_range	Write-back D-cache range
 *
 *	If I/D unified cache (SH3), I-cache ops are writeback invalidate
 *	operation.
 *	If write-through mode, sh_dcache_wb_range is no-operation.
 *
 */

#ifndef _SH_CACHE_H_
#define	_SH_CACHE_H_

#ifdef _KERNEL
struct sh_cache_ops {
	void (*_icache_sync_all)(void);
	void (*_icache_sync_range)(vaddr_t, vsize_t);
	void (*_icache_sync_range_index)(vaddr_t, vsize_t);

	void (*_dcache_wbinv_all)(void);
	void (*_dcache_wbinv_range)(vaddr_t, vsize_t);
	void (*_dcache_wbinv_range_index)(vaddr_t, vsize_t);
	void (*_dcache_inv_range)(vaddr_t, vsize_t);
	void (*_dcache_wb_range)(vaddr_t, vsize_t);
};

/* Cache configurations */
#define	sh_cache_enable_unified		sh_cache_enable_icache
extern int sh_cache_enable_icache;
extern int sh_cache_enable_dcache;
extern int sh_cache_write_through;
extern int sh_cache_write_through_p0_u0_p3;
extern int sh_cache_write_through_p1;
extern int sh_cache_ways;
extern int sh_cache_unified;
#define	sh_cache_size_unified		sh_cache_size_icache
extern int sh_cache_size_icache;
extern int sh_cache_size_dcache;
extern int sh_cache_line_size;
/* for n-way set associative cache */
extern int sh_cache_way_size;
extern int sh_cache_way_shift;
extern int sh_cache_entry_mask;

/* Special mode */
extern int sh_cache_ram_mode;
extern int sh_cache_index_mode_icache;
extern int sh_cache_index_mode_dcache;

extern int sh_cache_prefer_mask;

extern struct sh_cache_ops sh_cache_ops;

#define	sh_icache_sync_all()						\
	(*sh_cache_ops._icache_sync_all)()

#define	sh_icache_sync_range(v, s)					\
	(*sh_cache_ops._icache_sync_range)((v), (s))

#define	sh_icache_sync_range_index(v, s)				\
	(*sh_cache_ops._icache_sync_range_index)((v), (s))

#define	sh_dcache_wbinv_all()						\
	(*sh_cache_ops._dcache_wbinv_all)()

#define	sh_dcache_wbinv_range(v, s)					\
	(*sh_cache_ops._dcache_wbinv_range)((v), (s))

#define	sh_dcache_wbinv_range_index(v, s)				\
	(*sh_cache_ops._dcache_wbinv_range_index)((v), (s))

#define	sh_dcache_inv_range(v, s)					\
	(*sh_cache_ops._dcache_inv_range)((v), (s))

#define	sh_dcache_wb_range(v, s)					\
	(*sh_cache_ops._dcache_wb_range)((v), (s))

void sh_cache_init(void);
void sh_cache_information(void);

#define	SH_HAS_UNIFIED_CACHE	CPU_IS_SH3
#define	SH_HAS_VIRTUAL_ALIAS	CPU_IS_SH4
#define	SH_HAS_WRITEBACK_CACHE	(!sh_cache_write_through)

#endif /* _KERNEL */
#endif /* _SH_CACHE_H_ */

/*	$OpenBSD: cache_sh3.c,v 1.3 2016/03/05 17:16:33 tobiasu Exp $	*/
/*	$NetBSD: cache_sh3.c,v 1.12 2006/03/04 01:13:35 uwe Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>

#include <sh/cache.h>
#include <sh/cache_sh3.h>

#define	round_line(x)		(((x) + 15) & ~15)
#define	trunc_line(x)		((x) & ~15)

void sh3_cache_wbinv_all(void);
void sh3_cache_wbinv_range(vaddr_t, vsize_t);
void sh3_cache_wbinv_range_index(vaddr_t, vsize_t);
void sh3_cache_panic(vaddr_t, vsize_t);
void sh3_cache_nop(vaddr_t, vsize_t);

int sh_cache_way_size;
int sh_cache_way_shift;
int sh_cache_entry_mask;

static inline void cache_sh3_op_line_16_nway(int, vaddr_t, uint32_t);
static inline void cache_sh3_op_8lines_16_nway(int, vaddr_t, uint32_t);

void
sh3_cache_config(void)
{
	size_t cache_size;
	uint32_t r;

	/* Determine cache size */
	switch (cpu_product) {
	default:
		/* FALLTHROUGH */
	case CPU_PRODUCT_7708:
		/* FALLTHROUGH */
	case CPU_PRODUCT_7708S:
		/* FALLTHROUGH */
	case CPU_PRODUCT_7708R:
		cache_size = 8 * 1024;
		break;
	case CPU_PRODUCT_7709:
		cache_size = 8 * 1024;
		break;
	case CPU_PRODUCT_7709A:
		cache_size = 16 * 1024;
		break;
	}

	r = _reg_read_4(SH3_CCR);

	sh_cache_unified = 1;
	sh_cache_enable_unified = (r & SH3_CCR_CE);
	sh_cache_line_size = 16;
	sh_cache_write_through_p0_u0_p3 = r & SH3_CCR_WT;
	sh_cache_write_through_p1 = !(r & SH3_CCR_CB);
	sh_cache_write_through = sh_cache_write_through_p0_u0_p3 &&
	    sh_cache_write_through_p1;

	sh_cache_ram_mode = r & SH3_CCR_RA;
	if (sh_cache_ram_mode) {
		/*
		 * In RAM-mode, way 2 and 3 are used as RAM.
		 */
		sh_cache_ways = 2;
		sh_cache_size_unified = cache_size / 2;
	} else {
		sh_cache_ways = 4;
		sh_cache_size_unified = cache_size;
	}

	/* size enough to access foreach entries */
	sh_cache_way_size = sh_cache_size_unified / 4/*way*/;
	/* mask for extracting entry select */
	sh_cache_entry_mask = (sh_cache_way_size - 1) & ~15/*line-mask*/;
	/* shift for way selection (16KB/8KB) */
	sh_cache_way_shift =
	    /* entry bits */
	    ffs(sh_cache_size_unified / (4/*way*/ * 16/*line-size*/)) - 1
	    /* line bits */
	    + 4;

	sh_cache_ops._icache_sync_all		= sh3_cache_wbinv_all;
	sh_cache_ops._icache_sync_range		= sh3_cache_wbinv_range;
	sh_cache_ops._icache_sync_range_index	= sh3_cache_wbinv_range_index;
	sh_cache_ops._dcache_wbinv_all		= sh3_cache_wbinv_all;
	sh_cache_ops._dcache_wbinv_range	= sh3_cache_wbinv_range;
	sh_cache_ops._dcache_wbinv_range_index	= sh3_cache_wbinv_range_index;
	/* SH3 can't invalidate without write-back */
	sh_cache_ops._dcache_inv_range		= sh3_cache_panic;
	if (sh_cache_write_through) {
		sh_cache_ops._dcache_wb_range		= sh3_cache_nop;
	} else {
		/* SH3 can't write-back without invalidate */
		sh_cache_ops._dcache_wb_range		= sh3_cache_wbinv_range;
	}
}

/*
 * cache_sh3_op_line_16_nway: (index-operation)
 *
 *	Clear the specified bits on single 16-byte cache line. n-ways.
 *
 */
static inline void
cache_sh3_op_line_16_nway(int n, vaddr_t va, uint32_t bits)
{
	vaddr_t cca;
	int way;

	/* extract entry # */
	va &= sh_cache_entry_mask;

	/* operate for each way */
	for (way = 0; way < n; way++) {
		cca = (SH3_CCA | way << sh_cache_way_shift | va);
		_reg_bclr_4(cca, bits);
	}
}

/*
 * cache_sh3_op_8lines_16_nway: (index-operation)
 *
 *	Clear the specified bits on 8 16-byte cache lines, n-ways.
 *
 */
static inline void
cache_sh3_op_8lines_16_nway(int n, vaddr_t va, uint32_t bits)
{
	volatile uint32_t *cca;
	int way;

	/* extract entry # */
	va &= sh_cache_entry_mask;

	/* operate for each way */
	for (way = 0; way < n; way++) {
		cca = (volatile uint32_t *)
		    (SH3_CCA | way << sh_cache_way_shift | va);
		cca[ 0] &= ~bits;
		cca[ 4] &= ~bits;
		cca[ 8] &= ~bits;
		cca[12] &= ~bits;
		cca[16] &= ~bits;
		cca[20] &= ~bits;
		cca[24] &= ~bits;
		cca[28] &= ~bits;
	}
}

void
sh3_cache_wbinv_all(void)
{
	vaddr_t va;

	for (va = 0; va < sh_cache_way_size; va += 16 * 8)
		cache_sh3_op_8lines_16_nway(sh_cache_ways, va, CCA_U | CCA_V);
}

void
sh3_cache_wbinv_range_index(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);

	va = trunc_line(va);

	while ((eva - va) >= (8 * 16)) {
		cache_sh3_op_8lines_16_nway(sh_cache_ways, va, CCA_U | CCA_V);
		va += 16 * 8;
	}

	while (va < eva) {
		cache_sh3_op_line_16_nway(sh_cache_ways, va, CCA_U | CCA_V);
		va += 16;
	}
}

void
sh3_cache_wbinv_range(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	vaddr_t cca;

	va = trunc_line(va);

	while (va < eva) {
		cca = SH3_CCA | CCA_A | (va & sh_cache_entry_mask);
		/*
		 * extract virtual tag-address.
		 * MMU translates it to physical address tag,
		 * and write to address-array.
		 * implicitly specified U = 0, V = 0.
		 */
		_reg_write_4(cca, va & CCA_TAGADDR_MASK);
		va += 16;
	}
}

void
sh3_cache_panic(vaddr_t va, vsize_t size)
{
	panic("SH3 can't invalidate without write-back");
}

void
sh3_cache_nop(vaddr_t va, vsize_t sz)
{
	/* NO-OP */
}

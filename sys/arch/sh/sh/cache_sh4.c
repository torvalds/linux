/*	$OpenBSD: cache_sh4.c,v 1.6 2010/01/01 13:20:33 miod Exp $	*/
/*	$NetBSD: cache_sh4.c,v 1.15 2005/12/24 23:24:02 perry Exp $	*/

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
#include <sh/cache_sh4.h>

#define	round_line(x)		(((x) + 31) & ~31)
#define	trunc_line(x)		((x) & ~31)

void sh4_icache_sync_all(void);
void sh4_icache_sync_range(vaddr_t, vsize_t);
void sh4_icache_sync_range_index(vaddr_t, vsize_t);
void sh4_dcache_wbinv_all(void);
void sh4_dcache_wbinv_range(vaddr_t, vsize_t);
void sh4_dcache_wbinv_range_index(vaddr_t, vsize_t);
void sh4_dcache_inv_range(vaddr_t, vsize_t);
void sh4_dcache_wb_range(vaddr_t, vsize_t);

/* EMODE */
void sh4_emode_icache_sync_all(void);
void sh4_emode_icache_sync_range_index(vaddr_t, vsize_t);
void sh4_emode_dcache_wbinv_all(void);
void sh4_emode_dcache_wbinv_range_index(vaddr_t, vsize_t);

/* must be inlined. */
static inline void cache_sh4_op_line_32(vaddr_t, vaddr_t, uint32_t,
    uint32_t);
static inline void cache_sh4_op_8lines_32(vaddr_t, vaddr_t, uint32_t,
    uint32_t);
static inline void cache_sh4_emode_op_line_32(vaddr_t, vaddr_t,
    uint32_t, uint32_t, uint32_t);
static inline void cache_sh4_emode_op_8lines_32(vaddr_t, vaddr_t,
    uint32_t, uint32_t, uint32_t);

void
sh4_cache_config(void)
{
	int icache_size;
	int dcache_size;
	int ways;
	uint32_t r;

        /* Determine cache size */
	switch (cpu_product) {
	default:
		/* FALLTHROUGH */
	case CPU_PRODUCT_7750:
	case CPU_PRODUCT_7750S:
	case CPU_PRODUCT_7751:
#if defined(SH4_CACHE_DISABLE_EMODE)
	case CPU_PRODUCT_7750R:
	case CPU_PRODUCT_7751R:
#endif
		icache_size = SH4_ICACHE_SIZE;
		dcache_size = SH4_DCACHE_SIZE;
		ways = 1;
		r = SH4_CCR_ICE|SH4_CCR_OCE|SH4_CCR_WT;
		break;

#if !defined(SH4_CACHE_DISABLE_EMODE)
	case CPU_PRODUCT_7750R:
	case CPU_PRODUCT_7751R:
		icache_size = SH4_EMODE_ICACHE_SIZE;
		dcache_size = SH4_EMODE_DCACHE_SIZE;
		ways = 2;
		r = SH4_CCR_EMODE|SH4_CCR_ICE|SH4_CCR_OCE|SH4_CCR_WT;
		break;
#endif
	}
#if defined(SH4_CACHE_DISABLE_ICACHE)
	r &= ~SH4_CCR_ICE;
#endif
#if defined(SH4_CACHE_DISABLE_DCACHE)
	r &= ~SH4_CCR_OCE;
#endif
#if !defined (SH4_CACHE_WT)
#define	SH4_CACHE_WB_U0_P0_P3
#define	SH4_CACHE_WB_P1
#endif
#if defined(SH4_CACHE_WB_U0_P0_P3)
	r &= ~SH4_CCR_WT;
#endif
#if defined(SH4_CACHE_WB_P1)
	r |= SH4_CCR_CB;
#endif

	sh4_icache_sync_all();
	RUN_P2;
	_reg_write_4(SH4_CCR, SH4_CCR_ICI|SH4_CCR_OCI);
	_reg_write_4(SH4_CCR, r);
	RUN_P1;

	r = _reg_read_4(SH4_CCR);

	sh_cache_unified = 0;
	sh_cache_enable_icache = (r & SH4_CCR_ICE);
	sh_cache_enable_dcache = (r & SH4_CCR_OCE);
	sh_cache_ways = ways;
	sh_cache_line_size = SH4_CACHE_LINESZ;
	sh_cache_prefer_mask = (dcache_size / ways - 1);
	sh_cache_write_through_p0_u0_p3 = (r & SH4_CCR_WT);
	sh_cache_write_through_p1 = !(r & SH4_CCR_CB);
	sh_cache_write_through = sh_cache_write_through_p0_u0_p3 &&
	    sh_cache_write_through_p1;
	sh_cache_ram_mode = (r & SH4_CCR_ORA);
	sh_cache_index_mode_icache = (r & SH4_CCR_IIX);
	sh_cache_index_mode_dcache = (r & SH4_CCR_OIX);

	sh_cache_size_dcache = dcache_size;
	if (sh_cache_ram_mode)
		sh_cache_size_dcache /= 2;
	sh_cache_size_icache = icache_size;

	sh_cache_ops._icache_sync_all		= sh4_icache_sync_all;
	sh_cache_ops._icache_sync_range		= sh4_icache_sync_range;
	sh_cache_ops._icache_sync_range_index	= sh4_icache_sync_range_index;

	sh_cache_ops._dcache_wbinv_all		= sh4_dcache_wbinv_all;
	sh_cache_ops._dcache_wbinv_range	= sh4_dcache_wbinv_range;
	sh_cache_ops._dcache_wbinv_range_index	= sh4_dcache_wbinv_range_index;
	sh_cache_ops._dcache_inv_range		= sh4_dcache_inv_range;
	sh_cache_ops._dcache_wb_range		= sh4_dcache_wb_range;

	switch (cpu_product) {
	case CPU_PRODUCT_7750:
	case CPU_PRODUCT_7750S:
		/* memory mapped D$ can only be accessed from p2 */
		sh_cache_ops._dcache_wbinv_all =
		    (void *)SH3_P1SEG_TO_P2SEG(sh4_dcache_wbinv_all);
		sh_cache_ops._dcache_wbinv_range_index =
		    (void *)SH3_P1SEG_TO_P2SEG(sh4_dcache_wbinv_range_index);
		break;
	case CPU_PRODUCT_7750R:
	case CPU_PRODUCT_7751R:
		if (!(r & SH4_CCR_EMODE)) {
			break;
		}
		sh_cache_ops._icache_sync_all = sh4_emode_icache_sync_all;
		sh_cache_ops._icache_sync_range_index = sh4_emode_icache_sync_range_index;
		sh_cache_ops._dcache_wbinv_all = sh4_emode_dcache_wbinv_all;
		sh_cache_ops._dcache_wbinv_range_index = sh4_emode_dcache_wbinv_range_index;
		break;
	}
}

/*
 * cache_sh4_op_line_32: (index-operation)
 *
 *	Clear the specified bits on single 32-byte cache line.
 */
static inline void
cache_sh4_op_line_32(vaddr_t va, vaddr_t base, uint32_t mask, uint32_t bits)
{
	vaddr_t cca;

	cca = base | (va & mask);
	_reg_bclr_4(cca, bits);
}

/*
 * cache_sh4_op_8lines_32: (index-operation)
 *
 *	Clear the specified bits on 8 32-byte cache lines.
 */
static inline void
cache_sh4_op_8lines_32(vaddr_t va, vaddr_t base, uint32_t mask, uint32_t bits)
{
	volatile uint32_t *cca = (volatile uint32_t *)
	    (base | (va & mask));

	cca[ 0] &= ~bits;
	cca[ 8] &= ~bits;
	cca[16] &= ~bits;
	cca[24] &= ~bits;
	cca[32] &= ~bits;
	cca[40] &= ~bits;
	cca[48] &= ~bits;
	cca[56] &= ~bits;
}

void
sh4_icache_sync_all(void)
{
	vaddr_t va = 0;
	vaddr_t eva = SH4_ICACHE_SIZE;

	sh4_dcache_wbinv_all();

	RUN_P2;
	while (va < eva) {
		cache_sh4_op_8lines_32(va, SH4_CCIA, CCIA_ENTRY_MASK, CCIA_V);
		va += 32 * 8;
	}
	PAD_P1_SWITCH;
}

void
sh4_icache_sync_range(vaddr_t va, vsize_t sz)
{
	vaddr_t ccia;
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	sh4_dcache_wbinv_range(va, (eva - va));

	RUN_P2;
	while (va < eva) {
		/* CCR.IIX has no effect on this entry specification */
		ccia = SH4_CCIA | CCIA_A | (va & CCIA_ENTRY_MASK);
		_reg_write_4(ccia, va & CCIA_TAGADDR_MASK); /* V = 0 */
		va += 32;
	}
	PAD_P1_SWITCH;
}

void
sh4_icache_sync_range_index(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	sh4_dcache_wbinv_range_index(va, eva - va);

	RUN_P2;
	while ((eva - va) >= (8 * 32)) {
		cache_sh4_op_8lines_32(va, SH4_CCIA, CCIA_ENTRY_MASK, CCIA_V);
		va += 32 * 8;
	}

	while (va < eva) {
		cache_sh4_op_line_32(va, SH4_CCIA, CCIA_ENTRY_MASK, CCIA_V);
		va += 32;
	}
	PAD_P1_SWITCH;
}

void
sh4_dcache_wbinv_all(void)
{
	vaddr_t va = 0;
	vaddr_t eva = SH4_DCACHE_SIZE;

	/* RUN_P2; */ /* called via P2 address if necessary */
	while (va < eva) {
		cache_sh4_op_8lines_32(va, SH4_CCDA, CCDA_ENTRY_MASK,
		    (CCDA_U | CCDA_V));
		va += 32 * 8;
	}
	PAD_P1_SWITCH;
}

void
sh4_dcache_wbinv_range(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	while (va < eva) {
		__asm volatile("ocbp @%0" : : "r"(va));
		va += 32;
	}
}

void
sh4_dcache_wbinv_range_index(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	/* RUN_P2; */ /* called via P2 address if necessary */
	while ((eva - va) >= (8 * 32)) {
		cache_sh4_op_8lines_32(va, SH4_CCDA, CCDA_ENTRY_MASK,
		    (CCDA_U | CCDA_V));
		va += 32 * 8;
	}

	while (va < eva) {
		cache_sh4_op_line_32(va, SH4_CCDA, CCDA_ENTRY_MASK,
		    (CCDA_U | CCDA_V));
		va += 32;
	}
	PAD_P1_SWITCH;
}

void
sh4_dcache_inv_range(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	while (va < eva) {
		__asm volatile("ocbi @%0" : : "r"(va));
		va += 32;
	}
}

void
sh4_dcache_wb_range(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	while (va < eva) {
		__asm volatile("ocbwb @%0" : : "r"(va));
		va += 32;
	}
}

/*
 * EMODE operation
 */
/*
 * cache_sh4_emode_op_line_32: (index-operation)
 *
 *	Clear the specified bits on single 32-byte cache line. 2-ways.
 */
static inline void
cache_sh4_emode_op_line_32(vaddr_t va, vaddr_t base, uint32_t mask,
    uint32_t bits, uint32_t way_shift)
{
	vaddr_t cca;

	/* extract entry # */
	va &= mask;

	/* operate for each way */
	cca = base | (0 << way_shift) | va;
	_reg_bclr_4(cca, bits);

	cca = base | (1 << way_shift) | va;
	_reg_bclr_4(cca, bits);
}

/*
 * cache_sh4_emode_op_8lines_32: (index-operation)
 *
 *	Clear the specified bits on 8 32-byte cache lines. 2-ways.
 */
static inline void
cache_sh4_emode_op_8lines_32(vaddr_t va, vaddr_t base, uint32_t mask,
    uint32_t bits, uint32_t way_shift)
{
	volatile uint32_t *cca;

	/* extract entry # */
	va &= mask;

	/* operate for each way */
	cca = (volatile uint32_t *)(base | (0 << way_shift) | va);
	cca[ 0] &= ~bits;
	cca[ 8] &= ~bits;
	cca[16] &= ~bits;
	cca[24] &= ~bits;
	cca[32] &= ~bits;
	cca[40] &= ~bits;
	cca[48] &= ~bits;
	cca[56] &= ~bits;

	cca = (volatile uint32_t *)(base | (1 << way_shift) | va);
	cca[ 0] &= ~bits;
	cca[ 8] &= ~bits;
	cca[16] &= ~bits;
	cca[24] &= ~bits;
	cca[32] &= ~bits;
	cca[40] &= ~bits;
	cca[48] &= ~bits;
	cca[56] &= ~bits;
}

void
sh4_emode_icache_sync_all(void)
{
	vaddr_t va = 0;
	vaddr_t eva = SH4_ICACHE_SIZE;

	sh4_emode_dcache_wbinv_all();

	RUN_P2;
	while (va < eva) {
		cache_sh4_emode_op_8lines_32(va, SH4_CCIA, CCIA_ENTRY_MASK,
		    CCIA_V, 13);
		va += 32 * 8;
	}
	PAD_P1_SWITCH;
}

void
sh4_emode_icache_sync_range_index(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	sh4_emode_dcache_wbinv_range_index(va, eva - va);

	RUN_P2;
	while ((eva - va) >= (8 * 32)) {
		cache_sh4_emode_op_8lines_32(va, SH4_CCIA, CCIA_ENTRY_MASK,
		    CCIA_V, 13);
		va += 32 * 8;
	}

	while (va < eva) {
		cache_sh4_emode_op_line_32(va, SH4_CCIA, CCIA_ENTRY_MASK,
		    CCIA_V, 13);
		va += 32;
	}
	PAD_P1_SWITCH;
}

void
sh4_emode_dcache_wbinv_all(void)
{
	vaddr_t va = 0;
	vaddr_t eva = SH4_DCACHE_SIZE;

	while (va < eva) {
		cache_sh4_emode_op_8lines_32(va, SH4_CCDA, CCDA_ENTRY_MASK,
		    (CCDA_U | CCDA_V), 14);
		va += 32 * 8;
	}
}

void
sh4_emode_dcache_wbinv_range_index(vaddr_t va, vsize_t sz)
{
	vaddr_t eva = round_line(va + sz);
	va = trunc_line(va);

	while ((eva - va) >= (8 * 32)) {
		cache_sh4_emode_op_8lines_32(va, SH4_CCDA, CCDA_ENTRY_MASK,
		    (CCDA_U | CCDA_V), 14);
		va += 32 * 8;
	}

	while (va < eva) {
		cache_sh4_emode_op_line_32(va, SH4_CCDA, CCDA_ENTRY_MASK,
		    (CCDA_U | CCDA_V), 14);
		va += 32;
	}
}

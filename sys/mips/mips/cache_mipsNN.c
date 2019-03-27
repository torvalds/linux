/*	$NetBSD: cache_mipsNN.c,v 1.10 2005/12/24 20:07:19 perry Exp $	*/

/*
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Jason R. Thorpe and Simon Burge for Wasabi Systems, Inc.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/param.h>

#include <machine/cache.h>
#include <machine/cache_r4k.h>
#include <machine/cpuinfo.h>

#define	round_line16(x)		(((x) + 15) & ~15)
#define	trunc_line16(x)		((x) & ~15)

#define	round_line32(x)		(((x) + 31) & ~31)
#define	trunc_line32(x)		((x) & ~31)

#define	round_line64(x)		(((x) + 63) & ~63)
#define	trunc_line64(x)		((x) & ~63)

#define	round_line128(x)	(((x) + 127) & ~127)
#define	trunc_line128(x)	((x) & ~127)

#if defined(CPU_NLM)
static __inline void
xlp_sync(void)
{
        __asm __volatile (
	    ".set push              \n"
	    ".set noreorder         \n"
	    ".set mips64            \n"
	    "dla    $8, 1f          \n"
	    "/* jr.hb $8 */         \n"
	    ".word 0x1000408        \n"
	    "nop                    \n"
	 "1: nop                    \n"
	    ".set pop               \n"
	    : : : "$8");
}
#endif

#if defined(SB1250_PASS1)
#define	SYNC	__asm volatile("sync; sync")
#elif defined(CPU_NLM)
#define SYNC	xlp_sync()
#else
#define	SYNC	__asm volatile("sync")
#endif

#if defined(CPU_CNMIPS)
#define SYNCI  mips_sync_icache();
#elif defined(CPU_NLM)
#define SYNCI	xlp_sync()
#else
#define SYNCI
#endif

/*
 * Exported variables for consumers like bus_dma code
 */
int mips_picache_linesize;
int mips_pdcache_linesize;
int mips_sdcache_linesize;
int mips_dcache_max_linesize;

static int picache_size;
static int picache_stride;
static int picache_loopcount;
static int picache_way_mask;
static int pdcache_size;
static int pdcache_stride;
static int pdcache_loopcount;
static int pdcache_way_mask;
static int sdcache_size;
static int sdcache_stride;
static int sdcache_loopcount;
static int sdcache_way_mask;

void
mipsNN_cache_init(struct mips_cpuinfo * cpuinfo)
{
	int flush_multiple_lines_per_way;

	flush_multiple_lines_per_way = cpuinfo->l1.ic_nsets * cpuinfo->l1.ic_linesize * cpuinfo->l1.ic_linesize > PAGE_SIZE;
	if (cpuinfo->icache_virtual) {
		/*
		 * With a virtual Icache we don't need to flush
		 * multiples of the page size with index ops; we just
		 * need to flush one pages' worth.
		 */
		flush_multiple_lines_per_way = 0;
	}

	if (flush_multiple_lines_per_way) {
		picache_stride = PAGE_SIZE;
		picache_loopcount = (cpuinfo->l1.ic_nsets * cpuinfo->l1.ic_linesize / PAGE_SIZE) *
		    cpuinfo->l1.ic_nways;
	} else {
		picache_stride = cpuinfo->l1.ic_nsets * cpuinfo->l1.ic_linesize;
		picache_loopcount = cpuinfo->l1.ic_nways;
	}

	if (cpuinfo->l1.dc_nsets * cpuinfo->l1.dc_linesize < PAGE_SIZE) {
		pdcache_stride = cpuinfo->l1.dc_nsets * cpuinfo->l1.dc_linesize;
		pdcache_loopcount = cpuinfo->l1.dc_nways;
	} else {
		pdcache_stride = PAGE_SIZE;
		pdcache_loopcount = (cpuinfo->l1.dc_nsets * cpuinfo->l1.dc_linesize / PAGE_SIZE) *
		    cpuinfo->l1.dc_nways;
	}

	mips_picache_linesize = cpuinfo->l1.ic_linesize;
	mips_pdcache_linesize = cpuinfo->l1.dc_linesize;

	picache_size = cpuinfo->l1.ic_size;
	picache_way_mask = cpuinfo->l1.ic_nways - 1;
	pdcache_size = cpuinfo->l1.dc_size;
	pdcache_way_mask = cpuinfo->l1.dc_nways - 1;

	sdcache_stride = cpuinfo->l2.dc_nsets * cpuinfo->l2.dc_linesize;
	sdcache_loopcount = cpuinfo->l2.dc_nways;
	sdcache_size = cpuinfo->l2.dc_size;
	sdcache_way_mask = cpuinfo->l2.dc_nways - 1;

	mips_sdcache_linesize = cpuinfo->l2.dc_linesize;
	mips_dcache_max_linesize = MAX(mips_pdcache_linesize,
	    mips_sdcache_linesize);

#define CACHE_DEBUG
#ifdef CACHE_DEBUG
	printf("Cache info:\n");
	if (cpuinfo->icache_virtual)
		printf("  icache is virtual\n");
	printf("  picache_stride    = %d\n", picache_stride);
	printf("  picache_loopcount = %d\n", picache_loopcount);
	printf("  pdcache_stride    = %d\n", pdcache_stride);
	printf("  pdcache_loopcount = %d\n", pdcache_loopcount);
	printf("  max line size     = %d\n", mips_dcache_max_linesize);
#endif
}

void
mipsNN_icache_sync_all_16(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + picache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	mips_intern_dcache_wbinv_all();

	while (va < eva) {
		cache_r4k_op_32lines_16(va, CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += (32 * 16);
	}

	SYNC;
}

void
mipsNN_icache_sync_all_32(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + picache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	mips_intern_dcache_wbinv_all();

	while (va < eva) {
		cache_r4k_op_32lines_32(va, CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += (32 * 32);
	}

	SYNC;
}

void
mipsNN_icache_sync_all_64(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + picache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	mips_intern_dcache_wbinv_all();

	while (va < eva) {
		cache_r4k_op_32lines_64(va, CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += (32 * 64);
	}

	SYNC;
}

void
mipsNN_icache_sync_range_16(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line16(va + size);
	va = trunc_line16(va);

	mips_intern_dcache_wb_range(va, (eva - va));

	while ((eva - va) >= (32 * 16)) {
		cache_r4k_op_32lines_16(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += (32 * 16);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += 16;
	}

	SYNC;
}

void
mipsNN_icache_sync_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line32(va + size);
	va = trunc_line32(va);

	mips_intern_dcache_wb_range(va, (eva - va));

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += 32;
	}

	SYNC;
}

void
mipsNN_icache_sync_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line64(va + size);
	va = trunc_line64(va);

	mips_intern_dcache_wb_range(va, (eva - va));

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += 64;
	}

	SYNC;
}

void
mipsNN_icache_sync_range_index_16(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & picache_way_mask);

	eva = round_line16(va + size);
	va = trunc_line16(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = picache_stride;
	loopcount = picache_loopcount;

	mips_intern_dcache_wbinv_range_index(va, (eva - va));

	while ((eva - va) >= (8 * 16)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_8lines_16(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 8 * 16;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 16;
	}
}

void
mipsNN_icache_sync_range_index_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & picache_way_mask);

	eva = round_line32(va + size);
	va = trunc_line32(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = picache_stride;
	loopcount = picache_loopcount;

	mips_intern_dcache_wbinv_range_index(va, (eva - va));

	while ((eva - va) >= (8 * 32)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_8lines_32(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 8 * 32;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 32;
	}
}

void
mipsNN_icache_sync_range_index_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & picache_way_mask);

	eva = round_line64(va + size);
	va = trunc_line64(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = picache_stride;
	loopcount = picache_loopcount;

	mips_intern_dcache_wbinv_range_index(va, (eva - va));

	while ((eva - va) >= (8 * 64)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_8lines_64(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 8 * 64;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 64;
	}
}

void
mipsNN_pdcache_wbinv_all_16(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + pdcache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	while (va < eva) {
		cache_r4k_op_32lines_16(va,
		    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 16);
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_all_32(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + pdcache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	while (va < eva) {
		cache_r4k_op_32lines_32(va,
		    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 32);
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_all_64(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + pdcache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	while (va < eva) {
		cache_r4k_op_32lines_64(va,
		    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 64);
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_range_16(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line16(va + size);
	va = trunc_line16(va);

	while ((eva - va) >= (32 * 16)) {
		cache_r4k_op_32lines_16(va,
		    CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 16);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += 16;
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line32(va + size);
	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va,
		    CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += 32;
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line64(va + size);
	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va,
		    CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += 64;
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_range_index_16(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & pdcache_way_mask);

	eva = round_line16(va + size);
	va = trunc_line16(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = pdcache_stride;
	loopcount = pdcache_loopcount;

	while ((eva - va) >= (8 * 16)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_8lines_16(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 8 * 16;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 16;
	}
}

void
mipsNN_pdcache_wbinv_range_index_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & pdcache_way_mask);

	eva = round_line32(va + size);
	va = trunc_line32(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = pdcache_stride;
	loopcount = pdcache_loopcount;

	while ((eva - va) >= (8 * 32)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_8lines_32(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 8 * 32;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 32;
	}
}

void
mipsNN_pdcache_wbinv_range_index_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & pdcache_way_mask);

	eva = round_line64(va + size);
	va = trunc_line64(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = pdcache_stride;
	loopcount = pdcache_loopcount;

	while ((eva - va) >= (8 * 64)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_8lines_64(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 8 * 64;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 64;
	}
}
 
void
mipsNN_pdcache_inv_range_16(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line16(va + size);
	va = trunc_line16(va);

	while ((eva - va) >= (32 * 16)) {
		cache_r4k_op_32lines_16(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += (32 * 16);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += 16;
	}

	SYNC;
}

void
mipsNN_pdcache_inv_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line32(va + size);
	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += 32;
	}

	SYNC;
}

void
mipsNN_pdcache_inv_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line64(va + size);
	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += 64;
	}

	SYNC;
}

void
mipsNN_pdcache_wb_range_16(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line16(va + size);
	va = trunc_line16(va);

	while ((eva - va) >= (32 * 16)) {
		cache_r4k_op_32lines_16(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += (32 * 16);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += 16;
	}

	SYNC;
}

void
mipsNN_pdcache_wb_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line32(va + size);
	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += 32;
	}

	SYNC;
}

void
mipsNN_pdcache_wb_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line64(va + size);
	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += 64;
	}

	SYNC;
}

#ifdef CPU_CNMIPS

void
mipsNN_icache_sync_all_128(void)
{
        SYNCI
}

void
mipsNN_icache_sync_range_128(vm_offset_t va, vm_size_t size)
{
	SYNC;
}

void
mipsNN_icache_sync_range_index_128(vm_offset_t va, vm_size_t size)
{
}


void
mipsNN_pdcache_wbinv_all_128(void)
{
}


void
mipsNN_pdcache_wbinv_range_128(vm_offset_t va, vm_size_t size)
{
	SYNC;
}

void
mipsNN_pdcache_wbinv_range_index_128(vm_offset_t va, vm_size_t size)
{
}

void
mipsNN_pdcache_inv_range_128(vm_offset_t va, vm_size_t size)
{
}

void
mipsNN_pdcache_wb_range_128(vm_offset_t va, vm_size_t size)
{
	SYNC;
}

#else

void
mipsNN_icache_sync_all_128(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + picache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	mips_intern_dcache_wbinv_all();

	while (va < eva) {
		cache_r4k_op_32lines_128(va, CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += (32 * 128);
	}

	SYNC;
}

void
mipsNN_icache_sync_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line128(va + size);
	va = trunc_line128(va);

	mips_intern_dcache_wb_range(va, (eva - va));

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_I|CACHEOP_R4K_HIT_INV);
		va += 128;
	}

	SYNC;
}

void
mipsNN_icache_sync_range_index_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & picache_way_mask);

	eva = round_line128(va + size);
	va = trunc_line128(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = picache_stride;
	loopcount = picache_loopcount;

	mips_intern_dcache_wbinv_range_index(va, (eva - va));

	while ((eva - va) >= (32 * 128)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_32lines_128(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 32 * 128;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_I|CACHEOP_R4K_INDEX_INV);
		va += 128;
	}
}

void
mipsNN_pdcache_wbinv_all_128(void)
{
	vm_offset_t va, eva;

	va = MIPS_PHYS_TO_KSEG0(0);
	eva = va + pdcache_size;

	/*
	 * Since we're hitting the whole thing, we don't have to
	 * worry about the N different "ways".
	 */

	while (va < eva) {
		cache_r4k_op_32lines_128(va,
		    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 128);
	}

	SYNC;
}


void
mipsNN_pdcache_wbinv_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line128(va + size);
	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va,
		    CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB_INV);
		va += 128;
	}

	SYNC;
}

void
mipsNN_pdcache_wbinv_range_index_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva, tmpva;
	int i, stride, loopcount;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & pdcache_way_mask);

	eva = round_line128(va + size);
	va = trunc_line128(va);

	/*
	 * GCC generates better code in the loops if we reference local
	 * copies of these global variables.
	 */
	stride = pdcache_stride;
	loopcount = pdcache_loopcount;

	while ((eva - va) >= (32 * 128)) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_r4k_op_32lines_128(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 32 * 128;
	}

	while (va < eva) {
		tmpva = va;
		for (i = 0; i < loopcount; i++, tmpva += stride)
			cache_op_r4k_line(tmpva,
			    CACHE_R4K_D|CACHEOP_R4K_INDEX_WB_INV);
		va += 128;
	}
}

void
mipsNN_pdcache_inv_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line128(va + size);
	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_INV);
		va += 128;
	}

	SYNC;
}

void
mipsNN_pdcache_wb_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	eva = round_line128(va + size);
	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_D|CACHEOP_R4K_HIT_WB);
		va += 128;
	}

	SYNC;
}

#endif

void
mipsNN_sdcache_wbinv_all_32(void)
{
	vm_offset_t va = MIPS_PHYS_TO_KSEG0(0);
	vm_offset_t eva = va + sdcache_size;

	while (va < eva) {
		cache_r4k_op_32lines_32(va,
		    CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 32);
	}
}

void
mipsNN_sdcache_wbinv_all_64(void)
{
	vm_offset_t va = MIPS_PHYS_TO_KSEG0(0);
	vm_offset_t eva = va + sdcache_size;

	while (va < eva) {
		cache_r4k_op_32lines_64(va,
		    CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 64);
	}
}

void
mipsNN_sdcache_wbinv_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line32(va + size);

	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va,
		    CACHE_R4K_SD|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB_INV);
		va += 32;
	}
}

void
mipsNN_sdcache_wbinv_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line64(va + size);

	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va,
		    CACHE_R4K_SD|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB_INV);
		va += 64;
	}
}

void
mipsNN_sdcache_wbinv_range_index_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & (sdcache_size - 1));

	eva = round_line32(va + size);
	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va,
		    CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += 32;
	}
}

void
mipsNN_sdcache_wbinv_range_index_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & (sdcache_size - 1));

	eva = round_line64(va + size);
	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va,
		    CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += 64;
	}
}

void
mipsNN_sdcache_inv_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line32(va + size);

	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_INV);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_INV);
		va += 32;
	}
}

void
mipsNN_sdcache_inv_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line64(va + size);

	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_INV);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_INV);
		va += 64;
	}
}

void
mipsNN_sdcache_wb_range_32(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line32(va + size);

	va = trunc_line32(va);

	while ((eva - va) >= (32 * 32)) {
		cache_r4k_op_32lines_32(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB);
		va += (32 * 32);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB);
		va += 32;
	}
}

void
mipsNN_sdcache_wb_range_64(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line64(va + size);

	va = trunc_line64(va);

	while ((eva - va) >= (32 * 64)) {
		cache_r4k_op_32lines_64(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB);
		va += (32 * 64);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB);
		va += 64;
	}
}

void
mipsNN_sdcache_wbinv_all_128(void)
{
	vm_offset_t va = MIPS_PHYS_TO_KSEG0(0);
	vm_offset_t eva = va + sdcache_size;

	while (va < eva) {
		cache_r4k_op_32lines_128(va,
		    CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 128);
	}
}

void
mipsNN_sdcache_wbinv_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line128(va + size);

	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va,
		    CACHE_R4K_SD|CACHEOP_R4K_HIT_WB_INV);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB_INV);
		va += 128;
	}
}

void
mipsNN_sdcache_wbinv_range_index_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva;

	/*
	 * Since we're doing Index ops, we expect to not be able
	 * to access the address we've been given.  So, get the
	 * bits that determine the cache index, and make a KSEG0
	 * address out of them.
	 */
	va = MIPS_PHYS_TO_KSEG0(va & (sdcache_size - 1));

	eva = round_line128(va + size);
	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va,
		    CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_INDEX_WB_INV);
		va += 128;
	}
}

void
mipsNN_sdcache_inv_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line128(va + size);

	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_INV);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_INV);
		va += 128;
	}
}

void
mipsNN_sdcache_wb_range_128(vm_offset_t va, vm_size_t size)
{
	vm_offset_t eva = round_line128(va + size);

	va = trunc_line128(va);

	while ((eva - va) >= (32 * 128)) {
		cache_r4k_op_32lines_128(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB);
		va += (32 * 128);
	}

	while (va < eva) {
		cache_op_r4k_line(va, CACHE_R4K_SD|CACHEOP_R4K_HIT_WB);
		va += 128;
	}
}

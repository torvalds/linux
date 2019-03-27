/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 - 2011 Marius Strobl <marius@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <machine/asi.h>
#include <machine/cache.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/mcntl.h>
#include <machine/lsu.h>
#include <machine/tlb.h>
#include <machine/tte.h>
#include <machine/vmparam.h>

#define	ZEUS_FTLB_ENTRIES	32
#define	ZEUS_STLB_ENTRIES	2048

/*
 * CPU-specific initialization for Fujitsu Zeus CPUs
 */
void
zeus_init(u_int cpu_impl)
{
	u_long val;

	/* Ensure the TSB Extension Registers hold 0 as TSB_Base. */

	stxa(AA_DMMU_TSB_PEXT_REG, ASI_DMMU, 0);
	stxa(AA_IMMU_TSB_PEXT_REG, ASI_IMMU, 0);
	membar(Sync);

	stxa(AA_DMMU_TSB_SEXT_REG, ASI_DMMU, 0);
	/*
	 * NB: the secondary context was removed from the iMMU.
	 */
	membar(Sync);

	stxa(AA_DMMU_TSB_NEXT_REG, ASI_DMMU, 0);
	stxa(AA_IMMU_TSB_NEXT_REG, ASI_IMMU, 0);
	membar(Sync);

	val = ldxa(AA_MCNTL, ASI_MCNTL);
	/* Ensure MCNTL_JPS1_TSBP is 0. */
	val &= ~MCNTL_JPS1_TSBP;
	/*
	 * Ensure 4-Mbyte page entries are stored in the 1024-entry, 2-way set
	 * associative TLB.
	 */
	val = (val & ~MCNTL_RMD_MASK) | MCNTL_RMD_1024;
	stxa(AA_MCNTL, ASI_MCNTL, val);
}

/*
 * Enable level 1 caches.
 */
void
zeus_cache_enable(u_int cpu_impl)
{
	u_long lsu;

	lsu = ldxa(0, ASI_LSU_CTL_REG);
	stxa(0, ASI_LSU_CTL_REG, lsu | LSU_IC | LSU_DC);
	flush(KERNBASE);
}

/*
 * Flush all lines from the level 1 caches.
 */
void
zeus_cache_flush(void)
{

	stxa_sync(0, ASI_FLUSH_L1I, 0);
}

/*
 * Flush a physical page from the data cache.  Data cache consistency is
 * maintained by hardware.
 */
void
zeus_dcache_page_inval(vm_paddr_t spa __unused)
{

}

/*
 * Flush a physical page from the intsruction cache.  Instruction cache
 * consistency is maintained by hardware.
 */
void
zeus_icache_page_inval(vm_paddr_t pa __unused)
{

}

/*
 * Flush all non-locked mappings from the TLBs.
 */
void
zeus_tlb_flush_nonlocked(void)
{

	stxa(TLB_DEMAP_ALL, ASI_DMMU_DEMAP, 0);
	stxa(TLB_DEMAP_ALL, ASI_IMMU_DEMAP, 0);
	flush(KERNBASE);
}

/*
 * Flush all user mappings from the TLBs.
 */
void
zeus_tlb_flush_user(void)
{
	u_long data, tag;
	u_int i, slot;

	for (i = 0; i < ZEUS_FTLB_ENTRIES; i++) {
		slot = TLB_DAR_SLOT(TLB_DAR_FTLB, i);
		data = ldxa(slot, ASI_DTLB_DATA_ACCESS_REG);
		tag = ldxa(slot, ASI_DTLB_TAG_READ_REG);
		if ((data & TD_V) != 0 && (data & TD_L) == 0 &&
		    TLB_TAR_CTX(tag) != TLB_CTX_KERNEL)
			stxa_sync(slot, ASI_DTLB_DATA_ACCESS_REG, 0);
		data = ldxa(slot, ASI_ITLB_DATA_ACCESS_REG);
		tag = ldxa(slot, ASI_ITLB_TAG_READ_REG);
		if ((data & TD_V) != 0 && (data & TD_L) == 0 &&
		    TLB_TAR_CTX(tag) != TLB_CTX_KERNEL)
			stxa_sync(slot, ASI_ITLB_DATA_ACCESS_REG, 0);
	}
	for (i = 0; i < ZEUS_STLB_ENTRIES; i++) {
		slot = TLB_DAR_SLOT(TLB_DAR_STLB, i);
		data = ldxa(slot, ASI_DTLB_DATA_ACCESS_REG);
		tag = ldxa(slot, ASI_DTLB_TAG_READ_REG);
		if ((data & TD_V) != 0 && (data & TD_L) == 0 &&
		    TLB_TAR_CTX(tag) != TLB_CTX_KERNEL)
			stxa_sync(slot, ASI_DTLB_DATA_ACCESS_REG, 0);
		data = ldxa(slot, ASI_ITLB_DATA_ACCESS_REG);
		tag = ldxa(slot, ASI_ITLB_TAG_READ_REG);
		if ((data & TD_V) != 0 && (data & TD_L) == 0 &&
		    TLB_TAR_CTX(tag) != TLB_CTX_KERNEL)
			stxa_sync(slot, ASI_ITLB_DATA_ACCESS_REG, 0);
	}
}

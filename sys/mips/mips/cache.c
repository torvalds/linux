/*      $NetBSD: cache.c,v 1.33 2005/12/24 23:24:01 perry Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-3-Clause
 *
 * Copyright 2001, 2002 Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
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

/*-
 * Copyright 2000, 2001
 * Broadcom Corporation. All rights reserved.
 * 
 * This software is furnished under license and may be used and copied only
 * in accordance with the following terms and conditions.  Subject to these
 * conditions, you may download, copy, install, use, modify and distribute
 * modified or unmodified copies of this software in source and/or binary
 * form. No title or ownership is transferred hereby.
 * 
 * 1) Any source code used, modified or distributed must reproduce and
 *    retain this copyright notice and list of conditions as they appear in
 *    the source file.
 * 
 * 2) No right is granted to use any trade name, trademark, or logo of
 *    Broadcom Corporation.  The "Broadcom Corporation" name may not be
 *    used to endorse or promote products derived from this software
 *    without the prior written permission of Broadcom Corporation.
 *
 * 3) THIS SOFTWARE IS PROVIDED "AS-IS" AND ANY EXPRESS OR IMPLIED
 *    WARRANTIES, INCLUDING BUT NOT LIMITED TO, ANY IMPLIED WARRANTIES OF
 *    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR
 *    NON-INFRINGEMENT ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM BE LIABLE
 *    FOR ANY DAMAGES WHATSOEVER, AND IN PARTICULAR, BROADCOM SHALL NOT BE
 *    LIABLE FOR DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 *    BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *    WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 *    OR OTHERWISE), EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/systm.h>

#include <machine/cpuinfo.h>
#include <machine/cache.h>

struct mips_cache_ops mips_cache_ops;

#if defined(MIPS_DISABLE_L1_CACHE) || defined(CPU_RMI) || defined(CPU_NLM)
static void
cache_noop(vm_offset_t va, vm_size_t size)
{
}
#endif

void
mips_config_cache(struct mips_cpuinfo * cpuinfo)
{

	switch (cpuinfo->l1.ic_linesize) {
	case 16:
		mips_cache_ops.mco_icache_sync_all = mipsNN_icache_sync_all_16;
		mips_cache_ops.mco_icache_sync_range =
		    mipsNN_icache_sync_range_16;
		mips_cache_ops.mco_icache_sync_range_index =
		    mipsNN_icache_sync_range_index_16;
		break;
	case 32:
		mips_cache_ops.mco_icache_sync_all = mipsNN_icache_sync_all_32;
		mips_cache_ops.mco_icache_sync_range =
		    mipsNN_icache_sync_range_32;
		mips_cache_ops.mco_icache_sync_range_index =
		    mipsNN_icache_sync_range_index_32;
		break;
	case 64:
		mips_cache_ops.mco_icache_sync_all = mipsNN_icache_sync_all_64;
		mips_cache_ops.mco_icache_sync_range =
		    mipsNN_icache_sync_range_64;
		mips_cache_ops.mco_icache_sync_range_index =
		    mipsNN_icache_sync_range_index_64;
		break;
	case 128:
		mips_cache_ops.mco_icache_sync_all = mipsNN_icache_sync_all_128;
		mips_cache_ops.mco_icache_sync_range =
		    mipsNN_icache_sync_range_128;
		mips_cache_ops.mco_icache_sync_range_index =
		    mipsNN_icache_sync_range_index_128;
		break;

#ifdef MIPS_DISABLE_L1_CACHE
	case 0:
		mips_cache_ops.mco_icache_sync_all = (void (*)(void))cache_noop;
		mips_cache_ops.mco_icache_sync_range = cache_noop;
		mips_cache_ops.mco_icache_sync_range_index = cache_noop;
		break;
#endif
	default:
		panic("no Icache ops for %d byte lines",
		    cpuinfo->l1.ic_linesize);
	}

	switch (cpuinfo->l1.dc_linesize) {
	case 16:
		mips_cache_ops.mco_pdcache_wbinv_all =
		    mips_cache_ops.mco_intern_pdcache_wbinv_all =
		    mipsNN_pdcache_wbinv_all_16;
		mips_cache_ops.mco_pdcache_wbinv_range =
		    mipsNN_pdcache_wbinv_range_16;
		mips_cache_ops.mco_pdcache_wbinv_range_index =
		    mips_cache_ops.mco_intern_pdcache_wbinv_range_index =
		    mipsNN_pdcache_wbinv_range_index_16;
		mips_cache_ops.mco_pdcache_inv_range =
		    mipsNN_pdcache_inv_range_16;
		mips_cache_ops.mco_pdcache_wb_range =
		    mips_cache_ops.mco_intern_pdcache_wb_range =
		    mipsNN_pdcache_wb_range_16;
		break;
	case 32:
		mips_cache_ops.mco_pdcache_wbinv_all =
		    mips_cache_ops.mco_intern_pdcache_wbinv_all =
		    mipsNN_pdcache_wbinv_all_32;
#if defined(CPU_RMI) || defined(CPU_NLM)
		mips_cache_ops.mco_pdcache_wbinv_range = cache_noop;
#else
		mips_cache_ops.mco_pdcache_wbinv_range =
		    mipsNN_pdcache_wbinv_range_32;
#endif
#if defined(CPU_RMI) || defined(CPU_NLM)
		mips_cache_ops.mco_pdcache_wbinv_range_index =
		    mips_cache_ops.mco_intern_pdcache_wbinv_range_index = cache_noop;
		mips_cache_ops.mco_pdcache_inv_range = cache_noop;
#else
		mips_cache_ops.mco_pdcache_wbinv_range_index =
		    mips_cache_ops.mco_intern_pdcache_wbinv_range_index =
		    mipsNN_pdcache_wbinv_range_index_32;
		mips_cache_ops.mco_pdcache_inv_range =
		    mipsNN_pdcache_inv_range_32;
#endif
#if defined(CPU_RMI) || defined(CPU_NLM)
		mips_cache_ops.mco_pdcache_wb_range =
		    mips_cache_ops.mco_intern_pdcache_wb_range = cache_noop;
#else
		mips_cache_ops.mco_pdcache_wb_range =
		    mips_cache_ops.mco_intern_pdcache_wb_range =
		    mipsNN_pdcache_wb_range_32;
#endif
		break;
	case 64:
		mips_cache_ops.mco_pdcache_wbinv_all =
		    mips_cache_ops.mco_intern_pdcache_wbinv_all =
		    mipsNN_pdcache_wbinv_all_64;
		mips_cache_ops.mco_pdcache_wbinv_range =
		    mipsNN_pdcache_wbinv_range_64;
		mips_cache_ops.mco_pdcache_wbinv_range_index =
		    mips_cache_ops.mco_intern_pdcache_wbinv_range_index =
		    mipsNN_pdcache_wbinv_range_index_64;
		mips_cache_ops.mco_pdcache_inv_range =
		    mipsNN_pdcache_inv_range_64;
		mips_cache_ops.mco_pdcache_wb_range =
		    mips_cache_ops.mco_intern_pdcache_wb_range =
		    mipsNN_pdcache_wb_range_64;
		break;
	case 128:
		mips_cache_ops.mco_pdcache_wbinv_all =
		    mips_cache_ops.mco_intern_pdcache_wbinv_all =
		    mipsNN_pdcache_wbinv_all_128;
		mips_cache_ops.mco_pdcache_wbinv_range =
		    mipsNN_pdcache_wbinv_range_128;
		mips_cache_ops.mco_pdcache_wbinv_range_index =
		    mips_cache_ops.mco_intern_pdcache_wbinv_range_index =
		    mipsNN_pdcache_wbinv_range_index_128;
		mips_cache_ops.mco_pdcache_inv_range =
		    mipsNN_pdcache_inv_range_128;
		mips_cache_ops.mco_pdcache_wb_range =
		    mips_cache_ops.mco_intern_pdcache_wb_range =
		    mipsNN_pdcache_wb_range_128;
		break;
#ifdef MIPS_DISABLE_L1_CACHE
	case 0:
		mips_cache_ops.mco_pdcache_wbinv_all =
		    mips_cache_ops.mco_intern_pdcache_wbinv_all =
		    (void (*)(void))cache_noop;
		mips_cache_ops.mco_pdcache_wbinv_range = cache_noop;
		mips_cache_ops.mco_pdcache_wbinv_range_index = cache_noop;
		mips_cache_ops.mco_intern_pdcache_wbinv_range_index =
		    cache_noop;
		mips_cache_ops.mco_pdcache_inv_range = cache_noop;
		mips_cache_ops.mco_pdcache_wb_range = cache_noop;
		mips_cache_ops.mco_intern_pdcache_wb_range = cache_noop;
		break;
#endif
	default:
		panic("no Dcache ops for %d byte lines",
		    cpuinfo->l1.dc_linesize);
	}

	mipsNN_cache_init(cpuinfo);

#if 0
	if (mips_cpu_flags &
	    (CPU_MIPS_D_CACHE_COHERENT | CPU_MIPS_I_D_CACHE_COHERENT)) {
#ifdef CACHE_DEBUG
		printf("  Dcache is coherent\n");
#endif
		mips_cache_ops.mco_pdcache_wbinv_all = 
		    (void (*)(void))cache_noop;
		mips_cache_ops.mco_pdcache_wbinv_range = cache_noop;
		mips_cache_ops.mco_pdcache_wbinv_range_index = cache_noop;
		mips_cache_ops.mco_pdcache_inv_range = cache_noop;
		mips_cache_ops.mco_pdcache_wb_range = cache_noop;
	}
	if (mips_cpu_flags & CPU_MIPS_I_D_CACHE_COHERENT) {
#ifdef CACHE_DEBUG
		printf("  Icache is coherent against Dcache\n");
#endif
		mips_cache_ops.mco_intern_pdcache_wbinv_all =
		    (void (*)(void))cache_noop;
		mips_cache_ops.mco_intern_pdcache_wbinv_range_index =
		    cache_noop;
		mips_cache_ops.mco_intern_pdcache_wb_range = cache_noop;
	}
#endif

	/* Check that all cache ops are set up. */
	/* must have primary Icache */
	if (cpuinfo->l1.ic_size) {   
		
		if (!mips_cache_ops.mco_icache_sync_all)
			panic("no icache_sync_all cache op");
		if (!mips_cache_ops.mco_icache_sync_range)
			panic("no icache_sync_range cache op");
		if (!mips_cache_ops.mco_icache_sync_range_index)
			panic("no icache_sync_range_index cache op");
	}
	/* must have primary Dcache */
	if (cpuinfo->l1.dc_size) {
		if (!mips_cache_ops.mco_pdcache_wbinv_all)
			panic("no pdcache_wbinv_all");
		if (!mips_cache_ops.mco_pdcache_wbinv_range)
			panic("no pdcache_wbinv_range");
		if (!mips_cache_ops.mco_pdcache_wbinv_range_index)
			panic("no pdcache_wbinv_range_index");
		if (!mips_cache_ops.mco_pdcache_inv_range)
			panic("no pdcache_inv_range");
		if (!mips_cache_ops.mco_pdcache_wb_range)
			panic("no pdcache_wb_range");
	}

	/* L2 data cache */
	if (!cpuinfo->l2.dc_size) {
		/* No L2 found, ignore */
		return;
	}

	switch (cpuinfo->l2.dc_linesize) {
	case 32:
		mips_cache_ops.mco_sdcache_wbinv_all =
			mipsNN_sdcache_wbinv_all_32;
		mips_cache_ops.mco_sdcache_wbinv_range =
			mipsNN_sdcache_wbinv_range_32;
		mips_cache_ops.mco_sdcache_wbinv_range_index =
			mipsNN_sdcache_wbinv_range_index_32;
		mips_cache_ops.mco_sdcache_inv_range =
			mipsNN_sdcache_inv_range_32;
		mips_cache_ops.mco_sdcache_wb_range =
			mipsNN_sdcache_wb_range_32;
		break;
	case 64:
		mips_cache_ops.mco_sdcache_wbinv_all =
			mipsNN_sdcache_wbinv_all_64;
		mips_cache_ops.mco_sdcache_wbinv_range =
			mipsNN_sdcache_wbinv_range_64;
		mips_cache_ops.mco_sdcache_wbinv_range_index =
			mipsNN_sdcache_wbinv_range_index_64;
		mips_cache_ops.mco_sdcache_inv_range =
			mipsNN_sdcache_inv_range_64;
		mips_cache_ops.mco_sdcache_wb_range =
			mipsNN_sdcache_wb_range_64;
		break;
	case 128:
		mips_cache_ops.mco_sdcache_wbinv_all =
			mipsNN_sdcache_wbinv_all_128;
		mips_cache_ops.mco_sdcache_wbinv_range =
			mipsNN_sdcache_wbinv_range_128;
		mips_cache_ops.mco_sdcache_wbinv_range_index =
			mipsNN_sdcache_wbinv_range_index_128;
		mips_cache_ops.mco_sdcache_inv_range =
			mipsNN_sdcache_inv_range_128;
		mips_cache_ops.mco_sdcache_wb_range =
			mipsNN_sdcache_wb_range_128;
		break;
	default:
#ifdef CACHE_DEBUG
		printf("  no sdcache ops for %d byte lines",
		    cpuinfo->l2.dc_linesize);
#endif
		break;
	}
}

/*	$OpenBSD: cache.c,v 1.5 2016/03/05 17:16:33 tobiasu Exp $	*/
/*	$NetBSD: cache.c,v 1.11 2006/01/02 23:37:34 uwe Exp $	*/

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
#include <sh/cache_sh4.h>

/*
 * __cache_flush is used before sh_cache_config() is called.
 */
static void __cache_flush(void);

struct sh_cache_ops sh_cache_ops = {
	._icache_sync_all = (void (*)(void))__cache_flush,
	._icache_sync_range = (void (*)(vaddr_t, vsize_t))__cache_flush,
	._icache_sync_range_index = (void (*)(vaddr_t, vsize_t))__cache_flush,
	._dcache_wbinv_all = (void (*)(void))__cache_flush,
	._dcache_wbinv_range = (void (*)(vaddr_t, vsize_t))__cache_flush,
	._dcache_wbinv_range_index = (void (*)(vaddr_t, vsize_t))__cache_flush,
	._dcache_inv_range = (void (*)(vaddr_t, vsize_t))__cache_flush,
	._dcache_wb_range = (void (*)(vaddr_t, vsize_t))__cache_flush
};

int sh_cache_enable_icache;
int sh_cache_enable_dcache;
int sh_cache_write_through;
int sh_cache_write_through_p0_u0_p3;
int sh_cache_write_through_p1;
int sh_cache_unified;
int sh_cache_ways;
int sh_cache_size_icache;
int sh_cache_size_dcache;
int sh_cache_line_size;
int sh_cache_ram_mode;
int sh_cache_index_mode_icache;
int sh_cache_index_mode_dcache;
int sh_cache_prefer_mask;

void
sh_cache_init(void)
{
#ifdef CACHE_DEBUG
	return;
#endif
#ifdef SH3
	if (CPU_IS_SH3)
		sh3_cache_config();
#endif
#ifdef SH4
	if (CPU_IS_SH4)
		sh4_cache_config();
#endif
}

void
sh_cache_information(void)
{
#ifdef CACHE_DEBUG
	printf("*** USE CPU INDEPENDENT CACHE OPS. ***\n");
	return;
#endif

	/* I-cache or I/D-unified cache */
	printf("cpu0: %dKB/%dB",
	       sh_cache_size_icache >> 10, sh_cache_line_size);
	if (sh_cache_ways > 1)
		printf(" %d-way associative", sh_cache_ways);
	else
		printf(" direct");
	if (sh_cache_unified)
		printf(" I/D-");
	else
		printf(" I-");
	printf("cache");
	if (!sh_cache_enable_icache)
		printf(" DISABLED");
	if (sh_cache_unified && sh_cache_ram_mode)
		printf(" RAM-mode");
	if (sh_cache_index_mode_icache)
		printf(" INDEX-mode");

	/* D-cache */
	if (!sh_cache_unified) {
		printf(", %dKB/%dB", sh_cache_size_dcache >> 10,
		    sh_cache_line_size);
		if (sh_cache_ways > 1)
			printf(" %d-way associative", sh_cache_ways);
		else
			printf(" direct");
		printf(" D-cache");
		if (!sh_cache_enable_dcache)
			printf(" DISABLED");
		if (sh_cache_ram_mode)
			printf(" RAM-mode");
		if (sh_cache_index_mode_dcache)
			printf(" INDEX-mode");
	}
	printf("\n");

#ifdef CACHE_DEBUG
	/* Write-through/back */
	printf("cpu0: P0, U0, P3 write-%s; P1 write-%s\n",
	    sh_cache_write_through_p0_u0_p3 ? "through" : "back",
	    sh_cache_write_through_p1 ? "through" : "back");
#endif
}

/*
 * CPU-independent cache flush.
 */
void
__cache_flush(void)
{
	volatile int *p = (int *)SH3_PHYS_TO_P1SEG(IOM_RAM_BEGIN);
	int i;
	int d;

	/* Flush D-Cache */
	/*
	 * Access address range [13:4].
	 * max:
	 * 16KB line-size 16B 4-way ... [11:4]  * 4
	 * 16KB line-size 32B 1-way ... [13:5]
	 */
	for (i = 0; i < 256/*entry*/ * 4/*way*/; i++) {
		d = *p;
		p += 4;	/* next line index (16B) */
	}

	/* Flush I-Cache */
	/*
	 * this code flush I-cache. but can't compile..
	 *  __asm volatile(".space 8192");
	 *
	 */
}

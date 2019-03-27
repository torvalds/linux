/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996
 *	The President and Fellows of Harvard College. All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by Harvard University.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Aaron Brown and
 *	Harvard University.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 2001 by Thomas Moestl <tmm@FreeBSD.org>.
 * Copyright (c) 2008, 2010 Marius Strobl <marius@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: @(#)cache.c	8.2 (Berkeley) 10/30/93
 *	from: NetBSD: cache.c,v 1.5 2000/12/06 01:47:50 mrg Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/pcpu.h>

#include <dev/ofw/openfirm.h>

#include <machine/cache.h>
#include <machine/tlb.h>
#include <machine/ver.h>

cache_enable_t *cache_enable;
cache_flush_t *cache_flush;
dcache_page_inval_t *dcache_page_inval;
icache_page_inval_t *icache_page_inval;

u_int dcache_color_ignore;

#define	OF_GET(h, n, v)	OF_getprop((h), (n), &(v), sizeof(v))

static u_int cache_new_prop(u_int cpu_impl);

static u_int
cache_new_prop(u_int cpu_impl)
{

	switch (cpu_impl) {
	case CPU_IMPL_ULTRASPARCIV:
	case CPU_IMPL_ULTRASPARCIVp:
		return (1);
	default:
		return (0);
	}
}

/*
 * Fill in the cache parameters using the CPU node.
 */
void
cache_init(struct pcpu *pcpu)
{
	u_long set;
	u_int use_new_prop;

	/*
	 * For CPUs which ignore TD_CV and support hardware unaliasing don't
	 * bother doing page coloring.  This is equal across all CPUs.
	 */
	if (pcpu->pc_cpuid == 0 && pcpu->pc_impl == CPU_IMPL_SPARC64V)
		dcache_color_ignore = 1;

	use_new_prop = cache_new_prop(pcpu->pc_impl);
	if (OF_GET(pcpu->pc_node, !use_new_prop ? "icache-size" :
	    "l1-icache-size", pcpu->pc_cache.ic_size) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "icache-line-size" :
	    "l1-icache-line-size", pcpu->pc_cache.ic_linesize) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "icache-associativity" :
	    "l1-icache-associativity", pcpu->pc_cache.ic_assoc) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "dcache-size" :
	    "l1-dcache-size", pcpu->pc_cache.dc_size) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "dcache-line-size" :
	    "l1-dcache-line-size", pcpu->pc_cache.dc_linesize) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "dcache-associativity" :
	    "l1-dcache-associativity", pcpu->pc_cache.dc_assoc) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "ecache-size" :
	    "l2-cache-size", pcpu->pc_cache.ec_size) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "ecache-line-size" :
	    "l2-cache-line-size", pcpu->pc_cache.ec_linesize) == -1 ||
	    OF_GET(pcpu->pc_node, !use_new_prop ? "ecache-associativity" :
	    "l2-cache-associativity", pcpu->pc_cache.ec_assoc) == -1)
		OF_panic("%s: could not retrieve cache parameters", __func__);

	set = pcpu->pc_cache.ic_size / pcpu->pc_cache.ic_assoc;
	if ((set & ~(1UL << (ffs(set) - 1))) != 0)
		OF_panic("%s: I$ set size not a power of 2", __func__);
	if ((pcpu->pc_cache.dc_size &
	    ~(1UL << (ffs(pcpu->pc_cache.dc_size) - 1))) != 0)
		OF_panic("%s: D$ size not a power of 2", __func__);
	/*
	 * For CPUs which don't support unaliasing in hardware ensure that
	 * the data cache doesn't have too many virtual colors.
	 */
	if (dcache_color_ignore == 0 && ((pcpu->pc_cache.dc_size /
	    pcpu->pc_cache.dc_assoc) / PAGE_SIZE) != DCACHE_COLORS)
		OF_panic("%s: too many D$ colors", __func__);
	set = pcpu->pc_cache.ec_size / pcpu->pc_cache.ec_assoc;
	if ((set & ~(1UL << (ffs(set) - 1))) != 0)
		OF_panic("%s: E$ set size not a power of 2", __func__);

	if (pcpu->pc_impl >= CPU_IMPL_ULTRASPARCIII) {
		cache_enable = cheetah_cache_enable;
		cache_flush = cheetah_cache_flush;
		dcache_page_inval = cheetah_dcache_page_inval;
		icache_page_inval = cheetah_icache_page_inval;
		tlb_flush_nonlocked = cheetah_tlb_flush_nonlocked;
		tlb_flush_user = cheetah_tlb_flush_user;
	} else if (pcpu->pc_impl == CPU_IMPL_SPARC64V) {
		cache_enable = zeus_cache_enable;
		cache_flush = zeus_cache_flush;
		dcache_page_inval = zeus_dcache_page_inval;
		icache_page_inval = zeus_icache_page_inval;
		tlb_flush_nonlocked = zeus_tlb_flush_nonlocked;
		tlb_flush_user = zeus_tlb_flush_user;
	} else if (pcpu->pc_impl >= CPU_IMPL_ULTRASPARCI &&
	    pcpu->pc_impl < CPU_IMPL_ULTRASPARCIII) {
		cache_enable = spitfire_cache_enable;
		cache_flush = spitfire_cache_flush;
		dcache_page_inval = spitfire_dcache_page_inval;
		icache_page_inval = spitfire_icache_page_inval;
		tlb_flush_nonlocked = spitfire_tlb_flush_nonlocked;
		tlb_flush_user = spitfire_tlb_flush_user;
	} else
		OF_panic("%s: unknown CPU", __func__);
}
